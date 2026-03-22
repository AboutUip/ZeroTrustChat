#pragma once

#include <vector>
#include <cstdint>

namespace ZChatIM {
    namespace mm1 {
        struct DeviceSessionEntry {
            std::vector<uint8_t> sessionId;
            std::vector<uint8_t> deviceId;
            uint64_t loginTimeMs;
            uint64_t lastActiveMs;
        };

        class DeviceSessionManager {
        public:
            // RegisterDeviceSession: 注册新设备会话；返回被踢出的 sessionId（可为空）
            // 当设备数超过 2 时，踢掉最早登录的设备。
            // 持久化：**`MM2::Mm1RegisterDeviceSession` → `mm1_device_sessions`**（须 **`MM2::Initialize`**；否则返回 **false**）。
            bool RegisterDeviceSession(
                const std::vector<uint8_t>& userId,
                const std::vector<uint8_t>& deviceId,
                const std::vector<uint8_t>& sessionId,
                uint64_t loginTimeMs,
                uint64_t lastActiveMs,
                std::vector<uint8_t>& outKickedSessionId);

            bool UpdateLastActive(const std::vector<uint8_t>& userId, const std::vector<uint8_t>& sessionId, uint64_t nowMs);

            std::vector<DeviceSessionEntry> GetDeviceSessions(const std::vector<uint8_t>& userId) const;

            void CleanupExpiredSessions(uint64_t nowMs);

            void ClearAllRegistrations();
        };
    } // namespace mm1
} // namespace ZChatIM

