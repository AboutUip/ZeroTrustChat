#pragma once

#include <vector>
#include <cstdint>

namespace ZChatIM {
    namespace mm1 {
        // =============================================================
        // 多设备会话管理器契约
        // =============================================================
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
            bool RegisterDeviceSession(
                const std::vector<uint8_t>& userId,
                const std::vector<uint8_t>& deviceId,
                const std::vector<uint8_t>& sessionId,
                uint64_t loginTimeMs,
                uint64_t lastActiveMs,
                std::vector<uint8_t>& outKickedSessionId);

            // UpdateLastActive: 更新某会话 lastActive
            // 返回 true 表示已更新；false 表示用户/会话不存在或无权限（与 JNI 契约对齐）
            bool UpdateLastActive(const std::vector<uint8_t>& userId, const std::vector<uint8_t>& sessionId, uint64_t nowMs);

            // GetDeviceSessions: 获取用户下所有设备会话（<=2）
            std::vector<DeviceSessionEntry> GetDeviceSessions(const std::vector<uint8_t>& userId) const;

            // CleanupExpiredSessions: 清理过期会话（若实现）
            void CleanupExpiredSessions(uint64_t nowMs);
        };
    } // namespace mm1
} // namespace ZChatIM

