// ZMKP v1：**用户口令** → **PBKDF2-HMAC-SHA256** → **AES-256-GCM** 包裹 **32B 消息主密钥**（**`mm2_message_key.bin`**）。
// 与 **ZMK1/2/3** 互斥择一；**不**替代 SQLCipher 域分离派生（主密钥解密后仍走 **`DeriveMetadataSqlcipherKeyFromMessageMaster`**）。

#include "mm2/storage/Crypto.h"
#include "Types.h"

#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace ZChatIM::mm2::detail::message_key_passphrase {

    namespace {

        static void WriteU32Le(uint32_t v, uint8_t* p)
        {
            p[0] = static_cast<uint8_t>(v & 0xFFU);
            p[1] = static_cast<uint8_t>((v >> 8U) & 0xFFU);
            p[2] = static_cast<uint8_t>((v >> 16U) & 0xFFU);
            p[3] = static_cast<uint8_t>((v >> 24U) & 0xFFU);
        }

        static bool ReadU32Le(const uint8_t* p, uint32_t& out)
        {
            out = static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8U)
                | (static_cast<uint32_t>(p[2]) << 16U) | (static_cast<uint32_t>(p[3]) << 24U);
            return true;
        }

    } // namespace

    static constexpr uint8_t kZmkpMagic[5] = {'Z', 'M', 'K', 'P', 1};

    constexpr uint32_t kPbkdf2Iterations = 200000U;
    constexpr size_t   kSaltBytes        = 16U;
    constexpr size_t   kZmkpV1FileBytes =
        5U + 4U + 4U + kSaltBytes + ZChatIM::NONCE_SIZE + ZChatIM::CRYPTO_KEY_SIZE + ZChatIM::AUTH_TAG_SIZE;

    bool IsZmkpV1(const std::vector<uint8_t>& raw)
    {
        return raw.size() == kZmkpV1FileBytes && std::memcmp(raw.data(), kZmkpMagic, sizeof(kZmkpMagic)) == 0;
    }

    bool ParseZmkpV1(const std::vector<uint8_t>& raw, std::string_view passphraseUtf8, std::vector<uint8_t>& outMaster32, std::string& errOut)
    {
        outMaster32.clear();
        errOut.clear();
        if (!IsZmkpV1(raw)) {
            errOut = "invalid ZMKP v1 container";
            return false;
        }
        uint32_t iterStored = 0;
        uint32_t saltLen    = 0;
        (void)ReadU32Le(raw.data() + 5U, iterStored);
        (void)ReadU32Le(raw.data() + 9U, saltLen);
        if (saltLen != kSaltBytes) {
            errOut = "ZMKP salt length mismatch";
            return false;
        }
        if (iterStored < 100000U || iterStored > 10000000U) {
            errOut = "ZMKP iterations out of allowed range";
            return false;
        }
        const uint8_t* salt   = raw.data() + 13U;
        const uint8_t* sealed = raw.data() + 13U + kSaltBytes;
        uint8_t        kek[ZChatIM::CRYPTO_KEY_SIZE]{};
        if (!Crypto::DeriveKeyPbkdf2HmacSha256(
                reinterpret_cast<const uint8_t*>(passphraseUtf8.data()),
                passphraseUtf8.size(),
                salt,
                kSaltBytes,
                static_cast<int>(iterStored),
                kek,
                sizeof(kek))) {
            std::memset(kek, 0, sizeof(kek));
            errOut = "ZMKP PBKDF2 failed";
            return false;
        }
        const uint8_t* nonce = sealed;
        const uint8_t* ct    = sealed + ZChatIM::NONCE_SIZE;
        const uint8_t* tg    = sealed + ZChatIM::NONCE_SIZE + ZChatIM::CRYPTO_KEY_SIZE;
        if (!Crypto::DecryptMessage(
                ct,
                ZChatIM::CRYPTO_KEY_SIZE,
                kek,
                sizeof(kek),
                nonce,
                ZChatIM::NONCE_SIZE,
                tg,
                ZChatIM::AUTH_TAG_SIZE,
                outMaster32)) {
            std::memset(kek, 0, sizeof(kek));
            errOut = "ZMKP decrypt failed (wrong passphrase or corrupt file)";
            return false;
        }
        std::memset(kek, 0, sizeof(kek));
        if (outMaster32.size() != ZChatIM::CRYPTO_KEY_SIZE) {
            errOut = "ZMKP plaintext size invalid";
            outMaster32.clear();
            return false;
        }
        return true;
    }

    bool BuildZmkpV1Blob(const uint8_t master32[ZChatIM::CRYPTO_KEY_SIZE], std::string_view passphraseUtf8, std::vector<uint8_t>& outBlob, std::string& errOut)
    {
        outBlob.clear();
        errOut.clear();
        if (master32 == nullptr || passphraseUtf8.empty()) {
            errOut = "BuildZmkpV1Blob: invalid input";
            return false;
        }
        std::vector<uint8_t> salt = Crypto::GenerateSecureRandom(kSaltBytes);
        if (salt.size() != kSaltBytes) {
            errOut = "BuildZmkpV1Blob: salt RNG failed";
            return false;
        }
        uint8_t kek[ZChatIM::CRYPTO_KEY_SIZE]{};
        if (!Crypto::DeriveKeyPbkdf2HmacSha256(
                reinterpret_cast<const uint8_t*>(passphraseUtf8.data()),
                passphraseUtf8.size(),
                salt.data(),
                salt.size(),
                static_cast<int>(kPbkdf2Iterations),
                kek,
                sizeof(kek))) {
            std::memset(kek, 0, sizeof(kek));
            errOut = "BuildZmkpV1Blob: PBKDF2 failed";
            return false;
        }
        std::vector<uint8_t> nonce = Crypto::GenerateNonce(ZChatIM::NONCE_SIZE);
        if (nonce.size() != ZChatIM::NONCE_SIZE) {
            std::memset(kek, 0, sizeof(kek));
            errOut = "BuildZmkpV1Blob: nonce RNG failed";
            return false;
        }
        std::vector<uint8_t> ciphertext;
        uint8_t              tag[ZChatIM::AUTH_TAG_SIZE]{};
        if (!Crypto::EncryptMessage(
                master32,
                ZChatIM::CRYPTO_KEY_SIZE,
                kek,
                sizeof(kek),
                nonce.data(),
                nonce.size(),
                ciphertext,
                tag,
                sizeof(tag))) {
            std::memset(kek, 0, sizeof(kek));
            errOut = "BuildZmkpV1Blob: GCM encrypt failed";
            return false;
        }
        std::memset(kek, 0, sizeof(kek));
        if (ciphertext.size() != ZChatIM::CRYPTO_KEY_SIZE) {
            errOut = "BuildZmkpV1Blob: ciphertext size mismatch";
            return false;
        }
        outBlob.resize(kZmkpV1FileBytes);
        uint8_t* w = outBlob.data();
        std::memcpy(w, kZmkpMagic, sizeof(kZmkpMagic));
        WriteU32Le(kPbkdf2Iterations, w + 5U);
        WriteU32Le(static_cast<uint32_t>(kSaltBytes), w + 9U);
        std::memcpy(w + 13U, salt.data(), kSaltBytes);
        const size_t sealedOff = 13U + kSaltBytes;
        std::memcpy(w + sealedOff, nonce.data(), ZChatIM::NONCE_SIZE);
        std::memcpy(w + sealedOff + ZChatIM::NONCE_SIZE, ciphertext.data(), ZChatIM::CRYPTO_KEY_SIZE);
        std::memcpy(w + sealedOff + ZChatIM::NONCE_SIZE + ZChatIM::CRYPTO_KEY_SIZE, tag, ZChatIM::AUTH_TAG_SIZE);
        return true;
    }

} // namespace ZChatIM::mm2::detail::message_key_passphrase
