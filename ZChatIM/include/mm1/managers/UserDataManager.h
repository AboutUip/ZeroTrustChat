#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace ZChatIM {
    namespace mm1 {
        // mm1_user_kv when MM2 inited; else in-proc (tests).
        class UserDataManager {
        public:
            UserDataManager();
            ~UserDataManager();

            UserDataManager(UserDataManager&& other) noexcept;
            UserDataManager& operator=(UserDataManager&& other) noexcept;

            UserDataManager(const UserDataManager&)            = delete;
            UserDataManager& operator=(const UserDataManager&) = delete;

            bool StoreUserData(const std::vector<uint8_t>& userId, int32_t type, const std::vector<uint8_t>& data);

            std::vector<uint8_t> GetUserData(const std::vector<uint8_t>& userId, int32_t type);

            bool DeleteUserData(const std::vector<uint8_t>& userId, int32_t type);

        private:
            struct Impl;
            std::unique_ptr<Impl> impl_;
        };
    } // namespace mm1
} // namespace ZChatIM

