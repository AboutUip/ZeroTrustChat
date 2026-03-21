#pragma once

#include <vector>
#include <cstdint>
#include <string>

namespace ZChatIM {
    namespace mm1 {
        // =============================================================
        // 证书固定管理器契约（SPKI SHA-256 Pin）
        // =============================================================
        class CertPinningManager {
        public:
            // Configure: 设置当前/备用公钥哈希（32 bytes）
            void ConfigurePinnedPublicKeyHashes(
                const std::vector<uint8_t>& currentSpkiSha256,
                const std::vector<uint8_t>& standbySpkiSha256);

            // VerifyPinnedServerCertificate:
            // clientId: 可为空，建议用客户端标识（如连接标识/IP hash）
            // presentedSpkiSha256: TLS 握手提取得到的 SPKI SHA-256
            // 返回 true 表示验证通过；失败会更新失败计数并可能进入封禁状态（由实现决定）
            bool VerifyPinnedServerCertificate(
                const std::vector<uint8_t>& clientId,
                const std::vector<uint8_t>& presentedSpkiSha256);

            // IsClientBanned: 查询是否封禁
            bool IsClientBanned(const std::vector<uint8_t>& clientId) const;

            // RecordFailure: 手动记录一次失败（可由实现的 Verify 内部调用）
            void RecordFailure(const std::vector<uint8_t>& clientId);

            // ClearBan: 管理员手动解除封禁
            void ClearBan(const std::vector<uint8_t>& clientId);
        };
    } // namespace mm1
} // namespace ZChatIM

