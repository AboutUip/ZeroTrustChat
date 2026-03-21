// SHA-256: Windows BCrypt; other platforms portable FIPS 180-4 style implementation.

#include "mm2/crypto/Sha256.h"

#include <algorithm>
#include <cstring>
#include <vector>

#ifdef _WIN32
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>
#    include <bcrypt.h>
#endif

namespace ZChatIM::crypto {

#ifndef _WIN32

    namespace {

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

        bool sha256_portable(const uint8_t* data, size_t length, uint8_t outDigest[32])
        {
            if (length > 0 && data == nullptr) {
                return false;
            }
            uint32_t state[8];
            std::memcpy(state, kInit, sizeof(state));

            uint8_t  buf[64]{};
            size_t   bufLen = 0;
            uint64_t bitLen = 0;

            auto processBlock = [&](const uint8_t* block) {
                compress(state, block);
            };

            while (length > 0) {
                const size_t take = std::min<size_t>(64 - bufLen, length);
                std::memcpy(buf + bufLen, data, take);
                bufLen += take;
                data += take;
                length -= take;
                bitLen += static_cast<uint64_t>(take) * 8ULL;

                if (bufLen == 64) {
                    processBlock(buf);
                    bufLen = 0;
                }
            }

            // padding: 0x80 then zeros until 56 mod 64, then 64-bit big-endian bit length
            buf[bufLen++] = 0x80U;
            if (bufLen > 56) {
                std::memset(buf + bufLen, 0, 64 - bufLen);
                processBlock(buf);
                bufLen = 0;
            }
            std::memset(buf + bufLen, 0, 56 - bufLen);
            // append bit length as big-endian uint64 at end of last block
            const uint64_t bits = bitLen;
            for (int i = 0; i < 8; ++i) {
                buf[56 + i] = static_cast<uint8_t>((bits >> (56 - i * 8)) & 0xFFU);
            }
            processBlock(buf);

            for (int i = 0; i < 8; ++i) {
                outDigest[i * 4 + 0] = static_cast<uint8_t>((state[i] >> 24) & 0xFFU);
                outDigest[i * 4 + 1] = static_cast<uint8_t>((state[i] >> 16) & 0xFFU);
                outDigest[i * 4 + 2] = static_cast<uint8_t>((state[i] >> 8) & 0xFFU);
                outDigest[i * 4 + 3] = static_cast<uint8_t>(state[i] & 0xFFU);
            }
            return true;
        }

    } // namespace

#endif

    bool Sha256(const uint8_t* data, size_t length, uint8_t outDigest[32])
    {
        if (outDigest == nullptr) {
            return false;
        }
        if (length > 0 && data == nullptr) {
            return false;
        }
#ifdef _WIN32
        BCRYPT_ALG_HANDLE hAlg = nullptr;
        if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) {
            return false;
        }
        BCRYPT_HASH_HANDLE hHash = nullptr;
        DWORD              objLen = 0;
        DWORD              cbData = 0;
        if (!BCRYPT_SUCCESS(BCryptGetProperty(
                hAlg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objLen), sizeof(DWORD), &cbData, 0))) {
            BCryptCloseAlgorithmProvider(hAlg, 0);
            return false;
        }
        std::vector<uint8_t> hashObj(static_cast<size_t>(objLen));
        if (!BCRYPT_SUCCESS(BCryptCreateHash(hAlg, &hHash, hashObj.data(), objLen, nullptr, 0, 0))) {
            BCryptCloseAlgorithmProvider(hAlg, 0);
            return false;
        }
        constexpr size_t kChunk = static_cast<size_t>(1) << 28;
        size_t           off    = 0;
        while (off < length) {
            const ULONG part = static_cast<ULONG>(std::min(kChunk, length - off));
            if (!BCRYPT_SUCCESS(BCryptHashData(hHash, const_cast<PUCHAR>(data + off), part, 0))) {
                BCryptDestroyHash(hHash);
                BCryptCloseAlgorithmProvider(hAlg, 0);
                return false;
            }
            off += part;
        }
        if (!BCRYPT_SUCCESS(BCryptFinishHash(hHash, outDigest, 32, 0))) {
            BCryptDestroyHash(hHash);
            BCryptCloseAlgorithmProvider(hAlg, 0);
            return false;
        }
        BCryptDestroyHash(hHash);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return true;
#else
        return sha256_portable(data, length, outDigest);
#endif
    }

} // namespace ZChatIM::crypto
