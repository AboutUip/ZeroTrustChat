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
            // 持久化：**`MM2::Mm1RegisterDeviceSession` → `mm1_device_sessions`**（须 **`MM2::Initialize`**；否则返回 **false**）。
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

            // CleanupExpiredSessions：`lastActiveMs` 早于 **30 分钟** idle（与 **04-Session.md** 一致）的登记移除
            void CleanupExpiredSessions(uint64_t nowMs);

            // 清空全部多设备登记（**SQLite `mm1_device_sessions`**；**`MM2` 未初始化** 时为 no-op）；供 **`MM1::EmergencyTrustedZoneWipe`** 等（**`CleanupAllData`** 删库后亦可调用）。
            void ClearAllRegistrations();
        };
    } // namespace mm1
} // namespace ZChatIM

