#pragma once

#include <vector>
#include <cstdint>

namespace ZChatIM {
    namespace mm2 {
        // =============================================================
        // 消息查询管理器契约（listMessages）
        // =============================================================
        class MessageQueryManager {
        public:
            // 线程安全：由 MM2 独占持有；List* 仅允许在 MM2 已持有 m_stateMutex 时调用（同线程可重入由 recursive_mutex 支持）。

            // listMessages(userId, count) -> array
            // null 语义：返回空数组表示无可用消息
            std::vector<std::vector<uint8_t>> ListMessages(const std::vector<uint8_t>& userId, int count);

            // MessageSync(lastMsgId / timestamp) 查询
            // Query by timestamp (milliseconds since epoch)
            std::vector<std::vector<uint8_t>> ListMessagesSinceTimestamp(
                const std::vector<uint8_t>& userId,
                uint64_t sinceTimestampMs,
                int count);

            // Query by last message id cursor
            std::vector<std::vector<uint8_t>> ListMessagesSinceMessageId(
                const std::vector<uint8_t>& userId,
                const std::vector<uint8_t>& lastMsgId,
                int count);
        };
    } // namespace mm2
} // namespace ZChatIM

