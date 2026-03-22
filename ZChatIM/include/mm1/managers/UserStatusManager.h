#pragma once

#include <cstdint>
#include <vector>

namespace ZChatIM {
    namespace mm1 {
        // mm1_user_status when MM2 inited; else in-proc map. Server is SoT.
        class UserStatusManager {
        public:
            void SetUserOnline(const std::vector<uint8_t>& userId, bool online);

            bool GetUserStatusOnline(const std::vector<uint8_t>& userId) const;

            bool GetUserStatus(const std::vector<uint8_t>& userId) const;

            void ClearAll();
        };
    } // namespace mm1
} // namespace ZChatIM

