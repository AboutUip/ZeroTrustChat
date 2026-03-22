// MessageRecallManager：撤回/删除须验 Ed25519；公钥来自 UserData（type 见 kUserDataEd25519PubkeyType）。
#include "mm1/managers/MessageRecallManager.h"

#include "common/Ed25519.h"
#include "common/Memory.h"
#include "mm1/MM1.h"
#include "mm2/MM2.h"
#include "Types.h"

#include <cstring>
#include <vector>

namespace ZChatIM::mm1 {

    namespace {

        constexpr size_t kEd25519PublicKeyBytes = 32;
        constexpr size_t kEd25519SignatureBytes = 64;
        constexpr char   kRecallSigDomain[]     = "ZChatIM|RecallMessage|v1";

        // 与 Java/客户端约定：将 32B Ed25519 公钥存于 UserData(type)。
        constexpr int32_t kUserDataEd25519PubkeyType = 0x45444A31; // 'EDJ1'

        bool buildRecallCanonicalPayload(
            const std::vector<uint8_t>& messageId,
            const std::vector<uint8_t>& senderId,
            std::vector<uint8_t>&       out)
        {
            out.clear();
            const size_t prefixLen = sizeof(kRecallSigDomain) - 1U;
            out.reserve(prefixLen + MESSAGE_ID_SIZE + USER_ID_SIZE);
            out.insert(out.end(), kRecallSigDomain, kRecallSigDomain + prefixLen);
            out.insert(out.end(), messageId.begin(), messageId.end());
            out.insert(out.end(), senderId.begin(), senderId.end());
            return true;
        }

    } // namespace

    bool MessageRecallManager::RecallMessage(
        const std::vector<uint8_t>& messageId,
        const std::vector<uint8_t>& senderId,
        const std::vector<uint8_t>& signatureEd25519)
    {
        std::lock_guard<std::recursive_mutex> lock(MM1::Instance().m_apiRecursiveMutex);

        if (messageId.size() != MESSAGE_ID_SIZE || senderId.size() != USER_ID_SIZE) {
            return false;
        }
        if (signatureEd25519.size() != kEd25519SignatureBytes) {
            return false;
        }

        const std::vector<uint8_t> pubkey =
            MM1::Instance().GetUserDataManager().GetUserData(senderId, kUserDataEd25519PubkeyType);
        if (pubkey.size() != kEd25519PublicKeyBytes) {
            return false;
        }

        std::vector<uint8_t> payload;
        buildRecallCanonicalPayload(messageId, senderId, payload);

        if (!common::Ed25519VerifyDetached(
                payload.data(),
                payload.size(),
                signatureEd25519.data(),
                pubkey.data())) {
            return false;
        }

        return mm2::MM2::Instance().DeleteMessage(messageId);
    }

    bool MessageRecallManager::DeleteMessage(
        const std::vector<uint8_t>& messageId,
        const std::vector<uint8_t>& senderId,
        const std::vector<uint8_t>& signatureEd25519)
    {
        return RecallMessage(messageId, senderId, signatureEd25519);
    }

} // namespace ZChatIM::mm1
