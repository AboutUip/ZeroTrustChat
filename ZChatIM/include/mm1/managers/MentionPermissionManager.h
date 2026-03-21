#pragma once

#include <vector>
#include <cstdint>

namespace ZChatIM {
    namespace mm1 {
        // =============================================================
        // Mention（@提及 / @ALL）权限与速率校验契约
        // =============================================================
        class MentionPermissionManager {
        public:
            // mentionType: 1=个人，2=@ALL
            // @个人：sender 必须在群内；mentionedUserIds 必须为群成员（至少需要存在性）
            // @ALL：sender 必须为群主/管理员，并满足每分钟最多3次
            bool ValidateMentionRequest(
                const std::vector<uint8_t>& groupId,
                const std::vector<uint8_t>& senderId,
                int32_t mentionType,
                const std::vector<std::vector<uint8_t>>& mentionedUserIds,
                uint64_t nowMs,
                const std::vector<uint8_t>& signatureEd25519);

            // RecordMentionAtAllUsage: 记录一次 @ALL 使用（由实现层在校验通过后调用）
            bool RecordMentionAtAllUsage(
                const std::vector<uint8_t>& groupId,
                const std::vector<uint8_t>& senderId,
                uint64_t nowMs);
        };
    } // namespace mm1
} // namespace ZChatIM

