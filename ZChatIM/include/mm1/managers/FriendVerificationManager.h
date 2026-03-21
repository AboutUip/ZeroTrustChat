#pragma once

#include <vector>
#include <cstdint>

namespace ZChatIM {
    namespace mm1 {
        // =============================================================
        // 好友验证管理器契约（签名校验/状态流转的前置校验）
        // =============================================================
        class FriendVerificationManager {
        public:
            // VerifyFriendRequestSignature(fromUserId,toUserId,timestamp,signature)
            bool VerifyFriendRequestSignature(
                const std::vector<uint8_t>& fromUserId,
                const std::vector<uint8_t>& toUserId,
                uint64_t timestampSeconds,
                const std::vector<uint8_t>& signatureEd25519) const;

            // VerifyFriendResponseSignature(requestId,accept,responderId,timestamp,signature)
            bool VerifyFriendResponseSignature(
                const std::vector<uint8_t>& requestId,
                bool accept,
                const std::vector<uint8_t>& responderId,
                uint64_t timestampSeconds,
                const std::vector<uint8_t>& signatureEd25519) const;

            // VerifyDeleteFriendSignature(userA,userB,timestamp,signature)
            bool VerifyDeleteFriendSignature(
                const std::vector<uint8_t>& userA,
                const std::vector<uint8_t>& userB,
                uint64_t timestampSeconds,
                const std::vector<uint8_t>& signatureEd25519) const;
        };
    } // namespace mm1
} // namespace ZChatIM

