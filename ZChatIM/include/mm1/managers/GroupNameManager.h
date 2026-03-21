#pragma once

#include <vector>
#include <cstdint>
#include <string>

namespace ZChatIM {
    namespace mm1 {
        // =============================================================
        // 群组名称修改管理器契约
        // =============================================================
        class GroupNameManager {
        public:
            // UpdateGroupName:
            // - updaterId: 群主/管理员 ID
            // - newGroupName: 新群名（UTF-8）
            // - nowMs: 当前时间（用于 1次/分钟 频率限制等）
            // 返回 true 表示允许并成功发起更新流程（实现层完成写入 .zdb 与广播）
            bool UpdateGroupName(
                const std::vector<uint8_t>& groupId,
                const std::vector<uint8_t>& updaterId,
                const std::string& newGroupName,
                uint64_t nowMs);
        };
    } // namespace mm1
} // namespace ZChatIM

