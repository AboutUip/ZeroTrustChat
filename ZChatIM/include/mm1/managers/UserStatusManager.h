#pragma once

#include <cstdint>
#include <vector>

namespace ZChatIM {
    namespace mm1 {
        // =============================================================
        // 用户在线/离线状态管理器契约
        // =============================================================
        // **`MM2::Initialize` 成功** 时：经 **`MM2::Mm1UpsertUserStatus`** → **`mm1_user_status`**（**`user_version=11`**）持久化**最后已知**在线态；**服务端**仍为权威（重启后 UI 可显示缓存直至同步刷新）。**MM2 未初始化** 时回退进程内 map（单测）。
        // =============================================================
        class UserStatusManager {
        public:
            // SetUserOnline(userId, online)
            void SetUserOnline(const std::vector<uint8_t>& userId, bool online);

            // GetUserStatusOnline(userId) -> true/false
            bool GetUserStatusOnline(const std::vector<uint8_t>& userId) const;

            // GetUserStatus(userId) -> online/ offline
            bool GetUserStatus(const std::vector<uint8_t>& userId) const;

            void ClearAll();
        };
    } // namespace mm1
} // namespace ZChatIM

