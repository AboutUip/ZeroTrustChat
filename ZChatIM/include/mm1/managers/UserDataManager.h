#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace ZChatIM {
    namespace mm1 {
        // =============================================================
        // 用户数据管理器契约
        // =============================================================
        // **`MM2::Initialize` 已执行**（典型 JNI 流程）时：经 **`MM2` → `mm1_user_kv`**（元数据库）**持久化**；
        // 否则回退进程内内存表（单测 / 未拉起 MM2 时）。供 JNI / **`MessageRecallManager`** 等绑定 Ed25519 公钥（类型 **`0x45444A31`**）等。
        // =============================================================
        class UserDataManager {
        public:
            UserDataManager();
            ~UserDataManager();

            UserDataManager(UserDataManager&& other) noexcept;
            UserDataManager& operator=(UserDataManager&& other) noexcept;

            UserDataManager(const UserDataManager&)            = delete;
            UserDataManager& operator=(const UserDataManager&) = delete;

            // storeUserData(userId, type, data) -> result
            bool StoreUserData(const std::vector<uint8_t>& userId, int32_t type, const std::vector<uint8_t>& data);

            // getUserData(userId, type) -> data/null
            // null 语义：返回空 vector 表示未找到/失败
            std::vector<uint8_t> GetUserData(const std::vector<uint8_t>& userId, int32_t type);

            // deleteUserData(userId, type) -> result
            bool DeleteUserData(const std::vector<uint8_t>& userId, int32_t type);

        private:
            struct Impl;
            std::unique_ptr<Impl> impl_;
        };
    } // namespace mm1
} // namespace ZChatIM

