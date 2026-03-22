#pragma once

#include <vector>
#include <cstdint>

namespace ZChatIM {
    namespace mm1 {
        class FriendVerificationManager {
        public:
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

            bool VerifyDeleteFriendSignature(
                const std::vector<uint8_t>& userA,
                const std::vector<uint8_t>& userB,
                uint64_t timestampSeconds,
                const std::vector<uint8_t>& signatureEd25519) const;
        };
    } // namespace mm1
} // namespace ZChatIM

