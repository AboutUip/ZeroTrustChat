#pragma once

#include <cstdint>
#include <vector>

namespace ZChatIM {
    namespace mm1 {
        // =============================================================
        // 账户注销管理器契约
        // =============================================================
        class AccountDeleteManager {
        public:
            // DeleteAccount:
            //  - reauthToken: 重新认证信息（由上层/Java提供）
            //  - secondConfirmToken: 二次确认信息
            // 返回 true 表示注销流程成功发起（实现层完成 Level3 销毁与标记）
            bool DeleteAccount(
                const std::vector<uint8_t>& userId,
                const std::vector<uint8_t>& reauthToken,
                const std::vector<uint8_t>& secondConfirmToken);

            // IsAccountDeleted(userId) -> true/false
            bool IsAccountDeleted(const std::vector<uint8_t>& userId) const;
        };
    } // namespace mm1
} // namespace ZChatIM

