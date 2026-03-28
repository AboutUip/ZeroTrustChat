#pragma once

#include <vector>
#include <cstdint>

namespace ZChatIM {
    namespace mm1 {

        struct SendFriendRequestResult {
            std::vector<uint8_t> requestId;
            bool                 duplicatePending{false};
        };

        class FriendManager {
        public:
            SendFriendRequestResult SendFriendRequest(
                const std::vector<uint8_t>& fromUserId,
                const std::vector<uint8_t>& toUserId,
                uint64_t timestampSeconds,
                const std::vector<uint8_t>& signatureEd25519);

            bool RespondFriendRequest(
                const std::vector<uint8_t>& requestId,
                bool accept,
                const std::vector<uint8_t>& responderId,
                uint64_t timestampSeconds,
                const std::vector<uint8_t>& signatureEd25519);

            // deleteFriend(userA, userB, timestamp, signature) -> result
            // - userA/userB: 两侧用户ID
            bool DeleteFriend(
                const std::vector<uint8_t>& userId,
                const std::vector<uint8_t>& friendId,
                uint64_t timestampSeconds,
                const std::vector<uint8_t>& signatureEd25519);

            std::vector<std::vector<uint8_t>> GetFriends(const std::vector<uint8_t>& userId);

            /** 待处理申请：每行 request_id(16)‖from_user(16)‖created_s(uint64 BE)。 */
            std::vector<std::vector<uint8_t>> ListPendingIncomingFriendRequests(const std::vector<uint8_t>& userId);
        };
    } // namespace mm1
} // namespace ZChatIM

