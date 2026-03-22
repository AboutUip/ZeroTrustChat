#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

namespace ZChatIM::crypto {

    // SHA-256（**OpenSSL 3** **`EVP_sha256`**）；与协议/存储摘要字节一致（标准 FIPS 180-4 输出）。
    bool Sha256(const uint8_t* data, size_t length, uint8_t outDigest[32]);

    // 增量 SHA-256（大文件流式，如 **`MM2::CompleteFile`**）。
    class Sha256Hasher {
    public:
        Sha256Hasher();
        ~Sha256Hasher();
        Sha256Hasher(Sha256Hasher&&) noexcept;
        Sha256Hasher& operator=(Sha256Hasher&&) noexcept;
        Sha256Hasher(const Sha256Hasher&)            = delete;
        Sha256Hasher& operator=(const Sha256Hasher&) = delete;

        void Reset();
        bool Update(const uint8_t* data, size_t length);
        bool Final(uint8_t outDigest[32]);

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace ZChatIM::crypto
