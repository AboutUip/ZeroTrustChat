#pragma once

// 认证会话管理器：进程内会话表；不负责网络与持久化。
// 限流/封禁：docs/03-Business/02-Auth.md（用户 10 次/分钟；若提供 clientIp 则另计 IP 5 次/分钟）。
// 注：`mm1::MM1` 持有本类成员；每实例独立状态（pimpl）。

#include <cstdint>
#include <memory>
#include <vector>

namespace ZChatIM
{
    namespace mm1
    {
        class AuthSessionManager
        {
        public:
            AuthSessionManager();
            ~AuthSessionManager();

            AuthSessionManager(AuthSessionManager&& other) noexcept;
            AuthSessionManager& operator=(AuthSessionManager&& other) noexcept;

            AuthSessionManager(const AuthSessionManager&)            = delete;
            AuthSessionManager& operator=(const AuthSessionManager&) = delete;

            // auth -> sessionId/null。clientIp 为空时仅用户级限流；生产 JNI 建议传入 IP 字节（如 IPv4 4B / IPv6 16B）以满足文档 userId+IP 索引。
            std::vector<uint8_t> Auth(
                const std::vector<uint8_t>& userId,
                const std::vector<uint8_t>& token,
                const std::vector<uint8_t>& clientIp = {});

            bool VerifySession(const std::vector<uint8_t>& sessionId);

            // 若会话有效且未过期，写出 **USER_ID_SIZE** 字节 principal；否则 **outUserId** 清空并返回 false。
            bool TryGetSessionUserId(const std::vector<uint8_t>& sessionId, std::vector<uint8_t>& outUserId);

            bool DestroySession(const std::vector<uint8_t>& sessionId);

            // 清空全部认证会话及限流/封禁内存态（**进程内**）。供 **`JniBridge::EmergencyWipe`** 等；**不**持久化。
            void ClearAllSessions();

            // --- 供 **`LocalAccountCredentialManager`**：与 **`Auth`** 同源限流/封禁，在口令校验前后分段调用 ---
            bool ConsumeAuthAttemptSlot(
                const std::vector<uint8_t>& userId,
                const std::vector<uint8_t>& clientIp = {});

            void OnAuthIdentityCheckFailed(
                const std::vector<uint8_t>& userId,
                const std::vector<uint8_t>& clientIp = {});

            std::vector<uint8_t> FinalizeAuthSuccess(
                const std::vector<uint8_t>& userId,
                const std::vector<uint8_t>& clientIp = {});

            void ClearAuthThrottleSuccess(
                const std::vector<uint8_t>& userId,
                const std::vector<uint8_t>& clientIp = {});

        private:
            struct Impl;
            std::unique_ptr<Impl> impl_;
        };
    } // namespace mm1
} // namespace ZChatIM
