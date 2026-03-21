#pragma once

#include <cstddef>
#include <cstdint>

namespace ZChatIM::crypto {

    // SHA-256 over arbitrary bytes (portable implementation). Returns false on failure.
    bool Sha256(const uint8_t* data, size_t length, uint8_t outDigest[32]);

    // Incremental SHA-256 (portable FIPS 180-4); use for large inputs (e.g. file chunk streams).
    class Sha256Hasher {
    public:
        Sha256Hasher();
        void Reset();
        bool Update(const uint8_t* data, size_t length);
        bool Final(uint8_t outDigest[32]);

    private:
        uint32_t state_[8]{};
        uint8_t  buf_[64]{};
        size_t   bufLen_ = 0;
        uint64_t totalBitsLow_ = 0; // bits processed mod 2^64 (sufficient for practical file sizes)
    };

} // namespace ZChatIM::crypto
