// FriendVerificationManager：Ed25519 验签（canonical payload）；公钥来自 UserData（与 Recall / Reply 同 type）。
#include "mm1/managers/FriendVerificationManager.h"

#include "common/Ed25519.h"
#include "mm1/MM1.h"
#include "Types.h"

#include <vector>

namespace ZChatIM::mm1 {

    namespace {

        constexpr size_t kEd25519PublicKeyBytes = 32;
        constexpr size_t kEd25519SignatureBytes = 64;
        constexpr int32_t kUserDataEd25519PubkeyType = static_cast<int32_t>(0x45444A31); // 'EDJ1'

        constexpr char kSendFriendDomain[]     = "ZChatIM|SendFriendRequest|v1";
        constexpr char kRespondFriendDomain[]  = "ZChatIM|RespondFriendRequest|v1";
        constexpr char kDeleteFriendDomain[]   = "ZChatIM|DeleteFriend|v1";

        void AppendBe64(std::vector<uint8_t>& row, uint64_t v)
        {
            for (int s = 56; s >= 0; s -= 8) {
                row.push_back(static_cast<uint8_t>((v >> s) & 0xFF));
            }
        }

    } // namespace

    bool FriendVerificationManager::VerifyFriendRequestSignature(
        const std::vector<uint8_t>& fromUserId,
        const std::vector<uint8_t>& toUserId,
        uint64_t                    timestampSeconds,
        const std::vector<uint8_t>& signatureEd25519) const
    {
        if (fromUserId.size() != USER_ID_SIZE || toUserId.size() != USER_ID_SIZE) {
            return false;
        }
        if (signatureEd25519.size() != kEd25519SignatureBytes) {
            return false;
        }
        const std::vector<uint8_t> pubkey =
            MM1::Instance().GetUserDataManager().GetUserData(fromUserId, kUserDataEd25519PubkeyType);
        if (pubkey.size() != kEd25519PublicKeyBytes) {
            return false;
        }
        std::vector<uint8_t> payload;
        const size_t prefixLen = sizeof(kSendFriendDomain) - 1U;
        payload.reserve(prefixLen + USER_ID_SIZE * 2 + 8);
        payload.insert(payload.end(), kSendFriendDomain, kSendFriendDomain + prefixLen);
        payload.insert(payload.end(), fromUserId.begin(), fromUserId.end());
        payload.insert(payload.end(), toUserId.begin(), toUserId.end());
        AppendBe64(payload, timestampSeconds);
        return common::Ed25519VerifyDetached(
            payload.data(),
            payload.size(),
            signatureEd25519.data(),
            pubkey.data());
    }

    bool FriendVerificationManager::VerifyFriendResponseSignature(
        const std::vector<uint8_t>& requestId,
        bool                        accept,
        const std::vector<uint8_t>& responderId,
        uint64_t                    timestampSeconds,
        const std::vector<uint8_t>& signatureEd25519) const
    {
        if (requestId.size() != MESSAGE_ID_SIZE || responderId.size() != USER_ID_SIZE) {
            return false;
        }
        if (signatureEd25519.size() != kEd25519SignatureBytes) {
            return false;
        }
        const std::vector<uint8_t> pubkey =
            MM1::Instance().GetUserDataManager().GetUserData(responderId, kUserDataEd25519PubkeyType);
        if (pubkey.size() != kEd25519PublicKeyBytes) {
            return false;
        }
        std::vector<uint8_t> payload;
        const size_t prefixLen = sizeof(kRespondFriendDomain) - 1U;
        payload.reserve(prefixLen + MESSAGE_ID_SIZE + 1 + USER_ID_SIZE + 8);
        payload.insert(payload.end(), kRespondFriendDomain, kRespondFriendDomain + prefixLen);
        payload.insert(payload.end(), requestId.begin(), requestId.end());
        payload.push_back(accept ? static_cast<uint8_t>(1) : static_cast<uint8_t>(0));
        payload.insert(payload.end(), responderId.begin(), responderId.end());
        AppendBe64(payload, timestampSeconds);
        return common::Ed25519VerifyDetached(
            payload.data(),
            payload.size(),
            signatureEd25519.data(),
            pubkey.data());
    }

    bool FriendVerificationManager::VerifyDeleteFriendSignature(
        const std::vector<uint8_t>& userA,
        const std::vector<uint8_t>& userB,
        uint64_t                    timestampSeconds,
        const std::vector<uint8_t>& signatureEd25519) const
    {
        if (userA.size() != USER_ID_SIZE || userB.size() != USER_ID_SIZE) {
            return false;
        }
        if (signatureEd25519.size() != kEd25519SignatureBytes) {
            return false;
        }
        const std::vector<uint8_t> pubkey =
            MM1::Instance().GetUserDataManager().GetUserData(userA, kUserDataEd25519PubkeyType);
        if (pubkey.size() != kEd25519PublicKeyBytes) {
            return false;
        }
        std::vector<uint8_t> payload;
        const size_t prefixLen = sizeof(kDeleteFriendDomain) - 1U;
        payload.reserve(prefixLen + USER_ID_SIZE * 2 + 8);
        payload.insert(payload.end(), kDeleteFriendDomain, kDeleteFriendDomain + prefixLen);
        payload.insert(payload.end(), userA.begin(), userA.end());
        payload.insert(payload.end(), userB.begin(), userB.end());
        AppendBe64(payload, timestampSeconds);
        return common::Ed25519VerifyDetached(
            payload.data(),
            payload.size(),
            signatureEd25519.data(),
            pubkey.data());
    }

} // namespace ZChatIM::mm1
