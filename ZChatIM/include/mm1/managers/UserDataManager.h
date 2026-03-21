#pragma once

#include <vector>
#include <cstdint>

namespace ZChatIM {
    namespace mm1 {
        // =============================================================
        // 用户数据管理器契约
        // =============================================================
        class UserDataManager {
        public:
            // storeUserData(userId, type, data) -> result
            bool StoreUserData(const std::vector<uint8_t>& userId, int32_t type, const std::vector<uint8_t>& data);

            // getUserData(userId, type) -> data/null
            // null 语义：返回空 vector 表示未找到/失败
            std::vector<uint8_t> GetUserData(const std::vector<uint8_t>& userId, int32_t type);

            // deleteUserData(userId, type) -> result
            bool DeleteUserData(const std::vector<uint8_t>& userId, int32_t type);
        };
    } // namespace mm1
} // namespace ZChatIM

