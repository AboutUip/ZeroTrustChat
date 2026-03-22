#pragma once

// 本地账户：注册 / 口令登录 / 改密 / 恢复码重置（**mm1_user_kv** **LPH1** / **LRC1**）。
// 与 **`AuthSessionManager::Auth`** 并存： opaque **token** 登录不变；**`AuthWithLocalPassword`** 走 PBKDF2 校验后 **`FinalizeAuthSuccess`**。
// 须 **`MM2::Initialize`** 成功后方持久化。

#include <cstdint>
#include <vector>

namespace ZChatIM
{
    namespace mm1
    {
        class AuthSessionManager;

        class LocalAccountCredentialManager {
        public:
            LocalAccountCredentialManager();
            ~LocalAccountCredentialManager();

            LocalAccountCredentialManager(LocalAccountCredentialManager&&) noexcept            = delete;
            LocalAccountCredentialManager& operator=(LocalAccountCredentialManager&&) noexcept = delete;
            LocalAccountCredentialManager(const LocalAccountCredentialManager&)                = delete;
            LocalAccountCredentialManager& operator=(const LocalAccountCredentialManager&)     = delete;

            bool HasLocalPassword(const std::vector<uint8_t>& userId);

            bool RegisterLocalUser(
                const std::vector<uint8_t>& userId,
                const std::vector<uint8_t>& passwordUtf8,
                const std::vector<uint8_t>& recoverySecretUtf8);

            std::vector<uint8_t> AuthWithLocalPassword(
                AuthSessionManager&         auth,
                const std::vector<uint8_t>& userId,
                const std::vector<uint8_t>& passwordUtf8,
                const std::vector<uint8_t>& clientIp);

            bool ChangeLocalPassword(
                const std::vector<uint8_t>& userId,
                const std::vector<uint8_t>& oldPasswordUtf8,
                const std::vector<uint8_t>& newPasswordUtf8);

            // 与 **`Auth`** 同源限流：失败走 **`OnAuthIdentityCheckFailed`**；成功改密后 **`ClearAuthThrottleSuccess`**（**不**自动签发 session）。
            bool ResetLocalPasswordWithRecovery(
                AuthSessionManager&         auth,
                const std::vector<uint8_t>& userId,
                const std::vector<uint8_t>& recoverySecretUtf8,
                const std::vector<uint8_t>& newPasswordUtf8,
                const std::vector<uint8_t>& clientIp = {});
        };
    } // namespace mm1
} // namespace ZChatIM
