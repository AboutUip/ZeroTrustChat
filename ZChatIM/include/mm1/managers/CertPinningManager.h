#pragma once

#include <vector>
#include <cstdint>
#include <string>

namespace ZChatIM {
    namespace mm1 {
        // SPKI SHA-256 pin; optional TLS for ZSP transport.
        class CertPinningManager {
        public:
            void ConfigurePinnedPublicKeyHashes(
                const std::vector<uint8_t>& currentSpkiSha256,
                const std::vector<uint8_t>& standbySpkiSha256);

            // VerifyPinnedServerCertificate:
            // clientId: 可为空，建议用客户端标识（如连接标识/IP hash）
            // presentedSpkiSha256: 链路 TLS 握手提取得到的 SPKI SHA-256（常见为服务端证书）
            // 返回 true 表示验证通过；失败会更新失败计数并可能进入封禁状态（由实现决定）
            bool VerifyPinnedServerCertificate(
                const std::vector<uint8_t>& clientId,
                const std::vector<uint8_t>& presentedSpkiSha256);

            bool IsClientBanned(const std::vector<uint8_t>& clientId) const;

            void RecordFailure(const std::vector<uint8_t>& clientId);

            void ClearBan(const std::vector<uint8_t>& clientId);

            void ResetPinningState();
        };
    } // namespace mm1
} // namespace ZChatIM

