#pragma once

#include <map>
#include <string>
#include <vector>
#include <cstdint>

namespace ZChatIM {
    namespace mm1 {
        // =============================================================
        // 系统控制 / 安全运维管理器契约
        // 实现见 MM1_manager_stubs.cpp：委托 MM1::EmergencyTrustedZoneWipe /
        // SystemControlStatusSnapshot / RefreshMasterKey（与 JniSecurityPolicy.h 第8条 一致）。
        // =============================================================
        class SystemControl {
        public:
            // emergencyWipe() -> 紧急全量销毁（无返回）
            void EmergencyWipe();

            // getStatus() -> status（键值对）
            std::map<std::string, std::string> GetStatus();

            // rotateKeys() -> result
            bool RotateKeys();
        };
    } // namespace mm1
} // namespace ZChatIM

