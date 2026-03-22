#pragma once

#include <vector>
#include <cstdint>

namespace ZChatIM {
    namespace mm2 {

        class MM2;

        // =============================================================
        // 消息查询管理器（listMessages*）
        // -------------------------------------------------------------
        // 由 **MM2** 独占持有；**`SetOwner`** 在 **`MM2::Initialize` / `CleanupUnlocked`** 中维护。
        // **`List*`** 入口内部会对 **MM2** 加锁，可从任意线程直接调用（与 **`MM2` 其它 API** 一样串行化）。
        //
        // **参数 `userId`**：当前实现中与 **`imSessionId` / `GetSessionMessages` 的 `sessionId`** 相同，须 **`USER_ID_SIZE`（16）** 字节（单聊对方 ID 或会话通道 ID 由产品约定，存储层只认 16B BLOB）。
        //
        // **返回每条元素编码（与 JNI 对齐，大端长度）**：
        // **`message_id`（16B）‖ `payload_len`（`uint32` BE）‖ `payload`（明文）**。
        // =============================================================
        class MessageQueryManager {
        public:
            void SetOwner(MM2* mm2) { owner_ = mm2; }

            // 最近 `count` 条（与 **`MM2::GetSessionMessages(sessionId, count)`** 同序：插入顺序正序窗口）。
            // `count <= 0` 返回空。
            std::vector<std::vector<uint8_t>> ListMessages(const std::vector<uint8_t>& userId, int count);

            // 按 **进程内 RAM** 每条消息的 **`stored_at_ms`**（**`StoreMessage` 写入时**）**`>= sinceTimestampMs`** 取最多 **`count`** 条（**插入序**内过滤，与历史 SQLite **`stored_at_ms ASC, rowid ASC`** 语义对齐）。
            // **`count<=0`** 返回空；须 **`Initialize`** 且 **`userId`/`sessionId`** 为 16 字节。
            std::vector<std::vector<uint8_t>> ListMessagesSinceTimestamp(
                const std::vector<uint8_t>& userId,
                uint64_t sinceTimestampMs,
                int count);

            // `lastMsgId` **空**：从会话**最早**消息起取 `count` 条（**内存插入序**升序）。
            // `lastMsgId` **非空**：取严格**晚于**该 `message_id` 的后续 `count` 条（同会话）；若锚点不在本会话则结果为空。
            std::vector<std::vector<uint8_t>> ListMessagesSinceMessageId(
                const std::vector<uint8_t>& userId,
                const std::vector<uint8_t>& lastMsgId,
                int count);

        private:
            MM2* owner_ = nullptr;
        };
    } // namespace mm2
} // namespace ZChatIM

