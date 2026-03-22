#pragma once

#include <vector>
#include <cstdint>

namespace ZChatIM {
    namespace mm1 {
        class GroupMuteManager {
        public:
            bool MuteMember(
                const std::vector<uint8_t>& groupId,
                const std::vector<uint8_t>& userId,
                const std::vector<uint8_t>& mutedBy,
                uint64_t startTimeMs,
                int64_t durationSeconds,
                const std::vector<uint8_t>& reason);

            bool IsMuted(
                const std::vector<uint8_t>& groupId,
                const std::vector<uint8_t>& userId,
                uint64_t nowMs,
                int64_t& outRemainingSeconds) const;

            bool UnmuteMember(
                const std::vector<uint8_t>& groupId,
                const std::vector<uint8_t>& userId,
                const std::vector<uint8_t>& unmutedBy);

            void CleanupExpiredMutes(uint64_t nowMs);
        };
    } // namespace mm1
} // namespace ZChatIM
