#pragma once

#include <vector>

namespace ZChatIM {
    namespace mm1 {
        // =============================================================
        // 用户在线/离线状态管理器契约
        // =============================================================
        class UserStatusManager {
        public:
            // SetUserOnline(userId, online)
            void SetUserOnline(const std::vector<uint8_t>& userId, bool online);

            // GetUserStatusOnline(userId) -> true/false
            bool GetUserStatusOnline(const std::vector<uint8_t>& userId) const;

            // GetUserStatus(userId) -> online/ offline
            bool GetUserStatus(const std::vector<uint8_t>& userId) const;
        };
    } // namespace mm1
} // namespace ZChatIM

