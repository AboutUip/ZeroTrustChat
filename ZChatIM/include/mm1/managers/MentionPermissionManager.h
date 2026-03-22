#pragma once

#include <vector>
#include <cstdint>

namespace ZChatIM {
    namespace mm1 {
        // group_members + mm1_mention_atall_window. @ALL: 3 per 60s window.
        class MentionPermissionManager {
        public:
            bool ValidateMentionRequest(
                const std::vector<uint8_t>& groupId,
                const std::vector<uint8_t>& senderId,
                int32_t mentionType,
                const std::vector<std::vector<uint8_t>>& mentionedUserIds,
                uint64_t nowMs,
                const std::vector<uint8_t>& signatureEd25519);

            bool RecordMentionAtAllUsage(
                const std::vector<uint8_t>& groupId,
                const std::vector<uint8_t>& senderId,
                uint64_t nowMs);

            void ClearAtAllRateLimitState();
        };
    } // namespace mm1
} // namespace ZChatIM

