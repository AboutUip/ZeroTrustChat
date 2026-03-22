// MessageEditManager：Ed25519 验签（与 Recall 同 **UserData 0x45444A31**）+ **`MM2::EditMessage`**。
#include "mm1/managers/MessageEditManager.h"
#include "mm1/managers/MessageEditOrchestration.h"

#include "common/Ed25519.h"
#include "common/Memory.h"
#include "mm1/MM1.h"
#include "mm2/MM2.h"
#include "mm2/crypto/Sha256.h"
#include "Types.h"

#include <cstring>
#include <vector>

namespace ZChatIM::mm1 {

    namespace {

        constexpr size_t kEd25519PublicKeyBytes  = 32;
        constexpr size_t kEd25519SignatureBytes  = 64;
        constexpr int32_t kUserDataEd25519PubkeyType = 0x45444A31; // 'EDJ1'
        constexpr char    kEditSigDomain[]           = "ZChatIM|EditMessage|v1";

        void AppendU64Be(std::vector<uint8_t>& out, uint64_t v)
        {
            for (int s = 56; s >= 0; s -= 8) {
                out.push_back(static_cast<uint8_t>((v >> s) & 0xFF));
            }
        }

        bool BuildEditCanonical(
            const std::vector<uint8_t>& messageId,
            const std::vector<uint8_t>& senderId,
            uint64_t                    editTimestampSeconds,
            const uint8_t               contentDigest[32],
            std::vector<uint8_t>&       out)
        {
            out.clear();
            const size_t prefixLen = sizeof(kEditSigDomain) - 1U;
            out.reserve(prefixLen + MESSAGE_ID_SIZE + USER_ID_SIZE + 8 + 32);
            out.insert(out.end(), kEditSigDomain, kEditSigDomain + prefixLen);
            out.insert(out.end(), messageId.begin(), messageId.end());
            out.insert(out.end(), senderId.begin(), senderId.end());
            AppendU64Be(out, editTimestampSeconds);
            out.insert(out.end(), contentDigest, contentDigest + 32);
            return true;
        }

    } // namespace

    bool MessageEditManager::CheckEditAllowed(
        const std::vector<uint8_t>& messageId,
        const std::vector<uint8_t>& senderId,
        uint64_t                    editTimestampSeconds,
        const std::vector<uint8_t>& /*signature*/,
        uint32_t                    currentEditCount) const
    {
        std::lock_guard<std::recursive_mutex> lock(MM1::Instance().m_apiRecursiveMutex);
        if (messageId.size() != MESSAGE_ID_SIZE || senderId.size() != USER_ID_SIZE) {
            return false;
        }
        std::vector<uint8_t> storedSender;
        if (!mm2::MM2::Instance().GetMessageSenderUserId(messageId, storedSender)) {
            return false;
        }
        if (storedSender.size() != USER_ID_SIZE
            || !common::Memory::ConstantTimeCompare(storedSender.data(), senderId.data(), USER_ID_SIZE)) {
            return false;
        }
        uint32_t c = 0;
        uint64_t lt = 0;
        if (!mm2::MM2::Instance().GetMessageEditState(messageId, c, lt)) {
            return false;
        }
        if (c != currentEditCount) {
            return false;
        }
        if (c >= 3U) {
            return false;
        }
        if (c > 0U) {
            if (editTimestampSeconds < lt) {
                return false;
            }
            if (editTimestampSeconds - lt > 300ULL) {
                return false;
            }
        }
        return true;
    }

    bool MessageEditManager::GetEditState(
        const std::vector<uint8_t>& messageId,
        uint32_t&                   outEditCount,
        uint64_t&                   outLastEditTimeSeconds) const
    {
        std::lock_guard<std::recursive_mutex> lock(MM1::Instance().m_apiRecursiveMutex);
        return mm2::MM2::Instance().GetMessageEditState(messageId, outEditCount, outLastEditTimeSeconds);
    }

    bool MessageEditManager::ApplyEdit(
        const std::vector<uint8_t>& messageId,
        const std::vector<uint8_t>& newEncryptedContent,
        uint64_t                    editTimestampSeconds,
        const std::vector<uint8_t>& senderId,
        const std::vector<uint8_t>& signature)
    {
        std::lock_guard<std::recursive_mutex> lock(MM1::Instance().m_apiRecursiveMutex);

        if (messageId.size() != MESSAGE_ID_SIZE || senderId.size() != USER_ID_SIZE) {
            return false;
        }
        if (signature.size() != kEd25519SignatureBytes) {
            return false;
        }
        constexpr size_t kOverhead = ZChatIM::NONCE_SIZE + ZChatIM::AUTH_TAG_SIZE;
        if (newEncryptedContent.size() > ZChatIM::ZDB_MAX_WRITE_SIZE - kOverhead) {
            return false;
        }

        std::vector<uint8_t> storedSender;
        if (!mm2::MM2::Instance().GetMessageSenderUserId(messageId, storedSender)) {
            return false;
        }
        if (storedSender.size() != USER_ID_SIZE
            || !common::Memory::ConstantTimeCompare(storedSender.data(), senderId.data(), USER_ID_SIZE)) {
            return false;
        }

        const std::vector<uint8_t> pubkey =
            MM1::Instance().GetUserDataManager().GetUserData(senderId, kUserDataEd25519PubkeyType);
        if (pubkey.size() != kEd25519PublicKeyBytes) {
            return false;
        }

        uint8_t digest[32]{};
        if (!crypto::Sha256(
                newEncryptedContent.empty() ? nullptr : newEncryptedContent.data(),
                newEncryptedContent.size(),
                digest)) {
            return false;
        }

        std::vector<uint8_t> payload;
        BuildEditCanonical(messageId, senderId, editTimestampSeconds, digest, payload);

        if (!common::Ed25519VerifyDetached(
                payload.data(),
                payload.size(),
                signature.data(),
                pubkey.data())) {
            return false;
        }

        uint32_t c = 0;
        uint64_t lt = 0;
        if (!mm2::MM2::Instance().GetMessageEditState(messageId, c, lt)) {
            return false;
        }
        if (c >= 3U) {
            return false;
        }
        if (c > 0U) {
            if (editTimestampSeconds < lt) {
                return false;
            }
            if (editTimestampSeconds - lt > 300ULL) {
                return false;
            }
        }

        const uint32_t newEditCount = c + 1U;
        return mm2::MM2::Instance().EditMessage(
            messageId,
            newEncryptedContent,
            editTimestampSeconds,
            newEditCount);
    }

    bool MessageEditOrchestration::EditMessage(
        const std::vector<uint8_t>& messageId,
        const std::vector<uint8_t>& newEncryptedContent,
        uint64_t                    editTimestampSeconds,
        const std::vector<uint8_t>& signature,
        const std::vector<uint8_t>& senderId)
    {
        return MM1::Instance().GetMessageEditManager().ApplyEdit(
            messageId,
            newEncryptedContent,
            editTimestampSeconds,
            senderId,
            signature);
    }

} // namespace ZChatIM::mm1
