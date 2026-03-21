#pragma once

#include <vector>
#include <cstdint>

namespace ZChatIM {
    namespace mm1 {
        // =============================================================
        // 好友备注更新管理器契约
        // =============================================================
        class FriendNoteManager {
        public:
            // UpdateFriendNote:
            // - userId: 发起方用户ID
            // - friendId: 被备注的好友用户ID
            // - newEncryptedNote: 新备注内容（已由上层按安全策略加密后的密文/载荷）
            // - updateTimestampSeconds: 更新时间（秒）
            // - signatureEd25519: Ed25519 签名（供 MM1 验证身份后更新）
            // 返回 true 表示备注更新被接受并完成（实现层会进行签名与权限校验）
            bool UpdateFriendNote(
                const std::vector<uint8_t>& userId,
                const std::vector<uint8_t>& friendId,
                const std::vector<uint8_t>& newEncryptedNote,
                uint64_t updateTimestampSeconds,
                const std::vector<uint8_t>& signatureEd25519);
        };
    } // namespace mm1
} // namespace ZChatIM

