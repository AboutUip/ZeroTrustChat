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

            bool DestroySession(const std::vector<uint8_t>& sessionId);

        private:
            struct Impl;
            std::unique_ptr<Impl> impl_;
        };
    } // namespace mm1
} // namespace ZChatIM
