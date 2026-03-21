#pragma once

#include <vector>

namespace ZChatIM {
    namespace mm1 {
        // =============================================================
        // 消息回复安全校验 + 回复关系落库契约
        // =============================================================
        class MessageReplyManager {
        public:
            // StoreMessageReplyRelation:
            // - 在落库“回复关系”前完成安全校验：
            //   1) 验证回复者身份（群成员/好友成员等）
            //   2) 验证发送者身份签名（Ed25519）
            // - 成功后调用 MM2 存储 reply relation（实现层完成）
            bool StoreMessageReplyRelation(
                const std::vector<uint8_t>& messageId,
                const std::vector<uint8_t>& repliedMsgId,
                const std::vector<uint8_t>& repliedSenderId,
                const std::vector<uint8_t>& repliedContentDigest,
                const std::vector<uint8_t>& senderId,
                const std::vector<uint8_t>& signatureEd25519);
        };
    } // namespace mm1
} // namespace ZChatIM

