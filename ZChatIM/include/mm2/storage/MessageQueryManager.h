#pragma once

#include <vector>
#include <cstdint>

namespace ZChatIM {
    namespace mm2 {

        class MM2;

        // MM2-owned; List* locks MM2. userId == sessionId, 16B. Out: 16B id | BE u32 len | plaintext.
        class MessageQueryManager {
        public:
            void SetOwner(MM2* mm2) { owner_ = mm2; }

            // Same order as GetSessionMessages; count<=0 => empty.
            std::vector<std::vector<uint8_t>> ListMessages(const std::vector<uint8_t>& userId, int count);

            // RAM stored_at_ms filter; count<=0 => empty; need Init, 16B userId.
            std::vector<std::vector<uint8_t>> ListMessagesSinceTimestamp(
                const std::vector<uint8_t>& userId,
                uint64_t sinceTimestampMs,
                int count);

            // Empty lastMsgId: first N by insert order; else after that id in session.
            std::vector<std::vector<uint8_t>> ListMessagesSinceMessageId(
                const std::vector<uint8_t>& userId,
                const std::vector<uint8_t>& lastMsgId,
                int count);

        private:
            MM2* owner_ = nullptr;
        };
    } // namespace mm2
} // namespace ZChatIM

