#pragma once

#include <vector>
#include <cstdint>

namespace ZChatIM {
    namespace mm1 {
        // =============================================================
        // 消息编辑管理器契约
        // =============================================================
        class MessageEditManager {
        public:
            // CheckEditAllowed:
            //  - 5 分钟超时：timestamp - msgTimestamp < 300s
            //  - editCount < 3
            //  - 发送者身份校验与签名验证（Ed25519 由上层/实现决定）
            // 返回值：true 表示允许编辑。
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

            // RecordAndApplyEdit:
            // 由该接口完成“校验->记录 editCount/lastEditTime->调用 MM2 更新消息内容”（实现层完成）
            bool ApplyEdit(
                const std::vector<uint8_t>& messageId,
                const std::vector<uint8_t>& newEncryptedContent,
                uint64_t editTimestampSeconds,
                const std::vector<uint8_t>& senderId,
                const std::vector<uint8_t>& signature);
        };
    } // namespace mm1
} // namespace ZChatIM

