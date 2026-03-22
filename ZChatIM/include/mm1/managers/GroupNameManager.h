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
            // - updaterId: 群主/管理员 ID（须为 **`group_members`** 中 **role 1/2**）
            // - newGroupName: 新群名（UTF-8，**非空**，**≤2048 字节**，与 **`CreateGroup`** 一致）
            // - nowMs:  wall 毫秒；落库 **`mm2_group_display.updated_s`** 使用 **`nowMs / 1000`**（截断）
            // 返回 true 表示已写入 **`MM2::UpdateGroupName`**（元数据 SQLite；**非** `.zdb` 消息块）
            bool UpdateGroupName(
                const std::vector<uint8_t>& groupId,
                const std::vector<uint8_t>& updaterId,
                const std::string& newGroupName,
                uint64_t nowMs);
        };
    } // namespace mm1
} // namespace ZChatIM

