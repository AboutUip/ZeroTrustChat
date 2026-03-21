#pragma once

#include <vector>
#include <cstdint>

namespace ZChatIM {
    namespace mm1 {
        // =============================================================
        // 消息撤回管理器契约
        // =============================================================
        class MessageRecallManager {
        public:
            // RecallMessage:
            // - 仅消息发送者可撤回
            // - senderId/signatureEd25519 用于校验“撤回身份”
            // - 通过后触发 Level2 覆写/删除（实现层完成）
            bool RecallMessage(
                const std::vector<uint8_t>& messageId,
                const std::vector<uint8_t>& senderId,
                const std::vector<uint8_t>& signatureEd25519);

            // DeleteMessage:
            // - 为对齐 JNI 命名保留语义别名（安全入口同样需要 senderId/signatureEd25519）
            // - 行为等价于 RecallMessage（由实现层决定：Level2 覆写 + MM1 索引/状态同步）
            bool DeleteMessage(
                const std::vector<uint8_t>& messageId,
                const std::vector<uint8_t>& senderId,
                const std::vector<uint8_t>& signatureEd25519);
        };
    } // namespace mm1
} // namespace ZChatIM

