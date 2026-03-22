#pragma once

#include <vector>
#include <cstdint>

namespace ZChatIM {
    namespace mm1 {
        class MessageEditManager {
        public:
            bool CheckEditAllowed(
                const std::vector<uint8_t>& messageId,
                const std::vector<uint8_t>& senderId,
                uint64_t editTimestampSeconds,
                const std::vector<uint8_t>& signature,
                uint32_t currentEditCount) const;

            // GetEditState: 获取当前 editCount 与 lastEditTimeSeconds
            bool GetEditState(
                const std::vector<uint8_t>& messageId,
                uint32_t& outEditCount,
                uint64_t& outLastEditTimeSeconds) const;

            bool ApplyEdit(
                const std::vector<uint8_t>& messageId,
                const std::vector<uint8_t>& newEncryptedContent,
                uint64_t editTimestampSeconds,
                const std::vector<uint8_t>& senderId,
                const std::vector<uint8_t>& signature);
        };
    } // namespace mm1
} // namespace ZChatIM

