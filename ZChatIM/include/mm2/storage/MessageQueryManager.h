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

            // **`im_messages` 无服务器时间列**：**`count<=0`** 时返回空且不改 **`LastError`**；**`count>0`** 时返回空并 **`MM2::LastError`** 写明不支持（须 **`Initialize`** 且 **`sessionId`** 为 16 字节，否则先报对应错误）。
            std::vector<std::vector<uint8_t>> ListMessagesSinceTimestamp(
                const std::vector<uint8_t>& userId,
                uint64_t sinceTimestampMs,
                int count);

            // `lastMsgId` **空**：从会话**最早**消息起取 `count` 条（`rowid` 升序）。
            // `lastMsgId` **非空**：取严格**晚于**该 `message_id` 的后续 `count` 条（同会话）；若游标不在本会话则结果为空。
            std::vector<std::vector<uint8_t>> ListMessagesSinceMessageId(
                const std::vector<uint8_t>& userId,
                const std::vector<uint8_t>& lastMsgId,
                int count);

        private:
            MM2* owner_ = nullptr;
        };
    } // namespace mm2
} // namespace ZChatIM

