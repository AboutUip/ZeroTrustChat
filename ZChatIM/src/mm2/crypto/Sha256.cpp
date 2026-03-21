// SHA-256: portable FIPS 180-4 style implementation (all platforms).

#include "mm2/crypto/Sha256.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace ZChatIM::crypto {

    namespace detail {

        constexpr uint32_t kInit[8] = {
            0x6a09e667UL,
            0xbb67ae85UL,
            0x3c6ef372UL,
            0xa54ff53aUL,
            0x510e527fUL,
            0x9b05688cUL,
            0x1f83d9abUL,
            0x5be0cd19UL,
        };

        constexpr uint32_t kK[64] = {
            0x428a2f98UL, 0x71374491UL, 0xb5c0fbcfUL, 0xe9b5dba5UL, 0x3956c25bUL, 0x59f111f1UL, 0x923f82a4UL, 0xab1c5ed5UL,
            0xd807aa98UL, 0x12835b01UL, 0x243185beUL, 0x550c7dc3UL, 0x72be5d74UL, 0x80deb1feUL, 0x9bdc06a7UL, 0xc19bf174UL,
            0xe49b69c1UL, 0xefbe4786UL, 0x0fc19dc6UL, 0x240ca1ccUL, 0x2de92c6fUL, 0x4a7484aaUL, 0x5cb0a9dcUL, 0x76f988daUL,
            0x983e5152UL, 0xa831c66dUL, 0xb00327c8UL, 0xbf597fc7UL, 0xc6e00bf3UL, 0xd5a79147UL, 0x06ca6351UL, 0x14292967UL,
            0x27b70a85UL, 0x2e1b2138UL, 0x4d2c6dfcUL, 0x53380d13UL, 0x650a7354UL, 0x766a0abbUL, 0x81c2c92eUL, 0x92722c85UL,
            0xa2bfe8a1UL, 0xa81a664bUL, 0xc24b8b70UL, 0xc76c51a3UL, 0xd192e819UL, 0xd6990624UL, 0xf40e3585UL, 0x106aa070UL,
            0x19a4c116UL, 0x1e376c08UL, 0x2748774cUL, 0x34b0bcb5UL, 0x391c0cb3UL, 0x4ed8aa4aUL, 0x5b9cca4fUL, 0x682e6ff3UL,
            0x748f82eeUL, 0x78a5636fUL, 0x84c87814UL, 0x8cc70208UL, 0x90befffaUL, 0xa4506cebUL, 0xbef9a3f7UL, 0xc67178f2UL,
        };

        inline uint32_t rotr(uint32_t x, uint32_t n)
        {
            return (x >> n) | (x << (32U - n));
        }

        void compress(uint32_t state[8], const uint8_t block[64])
        {
            uint32_t w[64];
            for (int i = 0; i < 16; ++i) {
                w[i] = static_cast<uint32_t>(block[i * 4 + 0]) << 24 | static_cast<uint32_t>(block[i * 4 + 1]) << 16
                     | static_cast<uint32_t>(block[i * 4 + 2]) << 8 | static_cast<uint32_t>(block[i * 4 + 3]);
            }
            for (int i = 16; i < 64; ++i) {
                const uint32_t s0 = rotr(w[i - 15], 7U) ^ rotr(w[i - 15], 18U) ^ (w[i - 15] >> 3U);
                const uint32_t s1 = rotr(w[i - 2], 17U) ^ rotr(w[i - 2], 19U) ^ (w[i - 2] >> 10U);
                w[i] = w[i - 16] + s0 + w[i - 7] + s1;
            }

            uint32_t a = state[0];
            uint32_t b = state[1];
            uint32_t c = state[2];
            uint32_t d = state[3];
            uint32_t e = state[4];
            uint32_t f = state[5];
            uint32_t g = state[6];
            uint32_t h = state[7];

            for (int i = 0; i < 64; ++i) {
                const uint32_t S1  = rotr(e, 6U) ^ rotr(e, 11U) ^ rotr(e, 25U);
                const uint32_t ch  = (e & f) ^ ((~e) & g);
                const uint32_t t1  = h + S1 + ch + kK[i] + w[i];
                const uint32_t S0  = rotr(a, 2U) ^ rotr(a, 13U) ^ rotr(a, 22U);
                const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
                const uint32_t t2  = S0 + maj;

                h = g;
                g = f;
                f = e;
                e = d + t1;
                d = c;
                c = b;
                b = a;
                a = t1 + t2;
            }

            state[0] += a;
            state[1] += b;
            state[2] += c;
            state[3] += d;
            state[4] += e;
            state[5] += f;
            state[6] += g;
            state[7] += h;
        }

        void digestFromState(const uint32_t state[8], uint8_t outDigest[32])
        {
            for (int i = 0; i < 8; ++i) {
                outDigest[i * 4 + 0] = static_cast<uint8_t>((state[i] >> 24) & 0xFFU);
                outDigest[i * 4 + 1] = static_cast<uint8_t>((state[i] >> 16) & 0xFFU);
                outDigest[i * 4 + 2] = static_cast<uint8_t>((state[i] >> 8) & 0xFFU);
                outDigest[i * 4 + 3] = static_cast<uint8_t>(state[i] & 0xFFU);
            }
        }

    } // namespace

    Sha256Hasher::Sha256Hasher()
    {
        Reset();
    }

    void Sha256Hasher::Reset()
    {
        std::memcpy(state_, detail::kInit, sizeof(state_));
        std::memset(buf_, 0, sizeof(buf_));
        bufLen_       = 0;
        totalBitsLow_ = 0;
    }

    bool Sha256Hasher::Update(const uint8_t* data, size_t length)
    {
        if (length > 0 && data == nullptr) {
            return false;
        }
        while (length > 0) {
            const size_t take = std::min<size_t>(64 - bufLen_, length);
            std::memcpy(buf_ + bufLen_, data, take);
            bufLen_ += take;
            data += take;
            length -= take;
            totalBitsLow_ += static_cast<uint64_t>(take) * 8ULL;

            if (bufLen_ == 64) {
                detail::compress(state_, buf_);
                bufLen_ = 0;
            }
        }
        return true;
    }

    bool Sha256Hasher::Final(uint8_t outDigest[32])
    {
        if (outDigest == nullptr) {
            return false;
        }
        uint8_t pad[64]{};
        size_t  padLen = bufLen_;
        std::memcpy(pad, buf_, padLen);
        pad[padLen++] = 0x80U;

        if (padLen > 56) {
            std::memset(pad + padLen, 0, 64 - padLen);
            detail::compress(state_, pad);
            padLen = 0;
        }
        std::memset(pad + padLen, 0, 56 - padLen);
        const uint64_t bits = totalBitsLow_;
        for (int i = 0; i < 8; ++i) {
            pad[56 + i] = static_cast<uint8_t>((bits >> (56 - i * 8)) & 0xFFU);
        }
        detail::compress(state_, pad);
        detail::digestFromState(state_, outDigest);
        Reset();
        return true;
    }

    bool Sha256(const uint8_t* data, size_t length, uint8_t outDigest[32])
    {
        if (outDigest == nullptr) {
            return false;
        }
        if (length > 0 && data == nullptr) {
            return false;
        }
        Sha256Hasher h;
        if (!h.Update(data, length)) {
            return false;
        }
        return h.Final(outDigest);
    }

} // namespace ZChatIM::crypto
