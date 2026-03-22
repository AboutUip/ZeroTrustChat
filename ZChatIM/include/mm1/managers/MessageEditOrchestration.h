#pragma once

#include <vector>
#include <cstdint>

namespace ZChatIM {
    namespace mm1 {
        class MessageEditOrchestration {
        public:
            bool EditMessage(
                const std::vector<uint8_t>& messageId,
                const std::vector<uint8_t>& newEncryptedContent,
                uint64_t editTimestampSeconds,
                const std::vector<uint8_t>& signature,
                const std::vector<uint8_t>& senderId);
        };
    } // namespace mm1
} // namespace ZChatIM

