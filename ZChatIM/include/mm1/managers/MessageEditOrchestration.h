#pragma once

#include <vector>
#include <cstdint>

namespace ZChatIM {
    namespace mm1 {
        // =============================================================
        // 消息编辑编排管理器契约（可选抽象）
        // 将校验约束与 MM2 更新解耦
        // =============================================================
        class MessageEditOrchestration {
        public:
            // EditMessage: 完整编辑流程入口
            // - messageId: 被编辑消息ID
            // - newEncryptedContent: 新内容（按消息类型由上层加密后传入）
            // - editTimestampSeconds: 编辑时间（秒）
            // - signature: Ed25519 签名
            // - senderId: 原消息发送者ID（用于验证“仅发送方可编辑”）
            // 返回 true 表示编辑成功（实现层完成 editCount/lastEditTime 记录与消息更新）
            bool EditMessage(
                const std::vector<uint8_t>& messageId,
                const std::vector<uint8_t>& newEncryptedContent,
                uint64_t editTimestampSeconds,
                const std::vector<uint8_t>& signature,
                const std::vector<uint8_t>& senderId);
        };
    } // namespace mm1
} // namespace ZChatIM

