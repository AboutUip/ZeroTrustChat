#pragma once

#include <vector>
#include <cstdint>

namespace ZChatIM {
    namespace mm1 {
        // =============================================================
        // 群组禁言管理器契约
        // =============================================================
        class GroupMuteManager {
        public:
            // MuteMember: 群主/管理员禁言用户
            // durationSeconds: 禁言时长（-1 表示永久）
            // reason: 可选原因（加密/哈希后的标识由上层决定）
            // 返回 true 表示禁言已生效；false 表示权限不足/参数非法/失败（与 JNI 契约对齐）
            bool MuteMember(
                const std::vector<uint8_t>& groupId,
                const std::vector<uint8_t>& userId,
                const std::vector<uint8_t>& mutedBy,
                uint64_t startTimeMs,
                int64_t durationSeconds,
                const std::vector<uint8_t>& reason);

            // IsMuted: 检查禁言状态
            // nowMs: 当前时间
            // outRemainingSeconds: 若返回 true，则给出剩余秒数（永久可约定返回 -1）
            bool IsMuted(
                const std::vector<uint8_t>& groupId,
                const std::vector<uint8_t>& userId,
                uint64_t nowMs,
                int64_t& outRemainingSeconds) const;

            // UnmuteMember: 手动解禁（禁言操作者由上层传入）
            // 返回 true 表示解禁成功；false 表示无权限/非禁言状态/失败
            bool UnmuteMember(
                const std::vector<uint8_t>& groupId,
                const std::vector<uint8_t>& userId,
                const std::vector<uint8_t>& unmutedBy);

            // CleanupExpiredMutes: 清理到期禁言（若实现）
            void CleanupExpiredMutes(uint64_t nowMs);
        };
    } // namespace mm1
} // namespace ZChatIM

