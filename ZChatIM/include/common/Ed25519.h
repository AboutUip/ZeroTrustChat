#pragma once

#include <cstddef>
#include <cstdint>

namespace ZChatIM {
    namespace common {

        // Ed25519 分离签名验签（RFC 8032；与 OpenSSL **EVP** / Java **Ed25519** 等 64B 分离签名一致）。
        // **全平台**：OpenSSL 3 **EVP_PKEY_ED25519**（**`OpenSSL::Crypto`**）。
        bool Ed25519VerifyDetached(
            const uint8_t* message,
            size_t         messageLen,
            const uint8_t signature[64],
            const uint8_t publicKey[32]);

    } // namespace common
} // namespace ZChatIM
