#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace ZChatIM {
    namespace mm1 {
        // MM2 已初始化时：任意 type（含 Types.h 的 MM1_USER_KV_TYPE_AVATAR_V1 头像）均经
        // MM2::StoreMm1UserDataBlob / GetMm1UserDataBlob / DeleteMm1UserDataBlob 落库 mm1_user_kv。
        // 未初始化时：进程内 map（测试）。
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

