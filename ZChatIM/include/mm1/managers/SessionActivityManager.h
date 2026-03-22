#pragma once

// 即时通讯会话通道活跃时间（lastActive / idle 超时）。**持久化**：**`mm1_im_session_activity`**（经 **`MM2`**；须 **`MM2::Initialize`**）。
// 对应 JNI 的 imSessionId（16 字节）；与 ZSP Header 4 字节 SessionID 无关。参见 docs/03-Business/04-Session.md。

#include <cstdint>
#include <memory>
#include <vector>

namespace ZChatIM
{
    namespace mm1
    {
        class SessionActivityManager
        {
        public:
            SessionActivityManager();
            ~SessionActivityManager();

            SessionActivityManager(SessionActivityManager&& other) noexcept;
            SessionActivityManager& operator=(SessionActivityManager&& other) noexcept;

            SessionActivityManager(const SessionActivityManager&)            = delete;
            SessionActivityManager& operator=(const SessionActivityManager&) = delete;

            // nowMs 为 Unix 纪元毫秒；实现内相对本机时钟钳制，防止恶意超前时间延长 idle。
            void TouchSession(const std::vector<uint8_t>& sessionId, uint64_t nowMs);

            bool IsSessionExpired(const std::vector<uint8_t>& sessionId, uint64_t nowMs) const;

            // 使用当前系统 Unix 纪元毫秒时间判断是否仍活跃（与 TouchSession 传入的 nowMs 时钟域可能不同，由调用方保证一致策略）。
            bool GetSessionStatus(const std::vector<uint8_t>& sessionId) const;

            void CleanupExpiredSessions(uint64_t nowMs);

            void ClearAllTrackedSessions();

        private:
            struct Impl;
            std::unique_ptr<Impl> impl_;
        };
    } // namespace mm1
} // namespace ZChatIM
