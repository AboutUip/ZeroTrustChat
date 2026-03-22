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
            // - **callerSessionId**：**`AuthSessionManager::TryGetSessionUserId`** 成功（有效且未过期）；principal **必须**与 **senderId** 一致（常量时间比较）。JNI 侧应先 **`VerifySession`** 再调用本 API。
            // - **senderEd25519PublicKey**：Ed25519 公钥 **32B**；对下列 **canonical payload** 验证 **signatureEd25519**（64B 分离签名）。
            // - canonical：`"ZChatIM|StoreMessageReplyRelation|v1"` ‖ messageId(16) ‖ repliedMsgId(16) ‖
            //   repliedSenderId(16) ‖ repliedContentDigest(32) ‖ senderId(16)（**无**分隔符，定长拼接）。
            // - **群/好友可见性**：当前 native **无** imSession/好友图查询；由上层保证仅合法回复者调用；未来可在此接 MM2 元数据。
            // - 成功后调用 **MM2** 落库（`MessageReplyManager.cpp`；入口持 `MM1::m_apiRecursiveMutex`）。
            bool StoreMessageReplyRelation(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& senderEd25519PublicKey,
                const std::vector<uint8_t>& messageId,
                const std::vector<uint8_t>& repliedMsgId,
                const std::vector<uint8_t>& repliedSenderId,
                const std::vector<uint8_t>& repliedContentDigest,
                const std::vector<uint8_t>& senderId,
                const std::vector<uint8_t>& signatureEd25519);
        };
    } // namespace mm1
} // namespace ZChatIM
