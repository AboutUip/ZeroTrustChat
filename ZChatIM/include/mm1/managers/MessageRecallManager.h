#pragma once

#include <vector>
#include <cstdint>

namespace ZChatIM {
    namespace mm1 {
        class MessageRecallManager {
        public:
            bool RecallMessage(
                const std::vector<uint8_t>& messageId,
                const std::vector<uint8_t>& senderId,
                const std::vector<uint8_t>& signatureEd25519);

            bool DeleteMessage(
                const std::vector<uint8_t>& messageId,
                const std::vector<uint8_t>& senderId,
                const std::vector<uint8_t>& signatureEd25519);
        };
    } // namespace mm1
} // namespace ZChatIM

