// MM2 Crypto: wire format nonce(12) ‖ ciphertext ‖ tag(16); PBKDF2-HMAC-SHA256, 100000 iters.
// All platforms: OpenSSL 3 libcrypto (AES-GCM / PBKDF2 / RNG).

#include "mm2/storage/Crypto.h"
#include "mm2/crypto/Sha256.h"
#include "common/OpenSsl3Required.h"

#include <openssl/evp.h>
#include <openssl/rand.h>

#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>

namespace {

    // Serialize Init/Cleanup vs concurrent first-time Init (defense in depth; MM2 also holds its own lock).
    std::mutex g_cryptoInitMutex;

} // namespace

namespace ZChatIM::mm2 {

    namespace {

        bool ReadDevUrandom(void* buf, size_t len)
        {
#if defined(_WIN32)
            (void)buf;
            (void)len;
            return false;
#else
            if (len > static_cast<size_t>((std::numeric_limits<std::streamsize>::max)()))
                return false;
            std::ifstream f("/dev/urandom", std::ios::binary);
            if (!f.read(static_cast<char*>(buf), static_cast<std::streamsize>(len)))
                return false;
            return f.gcount() == static_cast<std::streamsize>(len);
#endif
        }

        using EvpCipherCtxPtr = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;

        EvpCipherCtxPtr MakeCipherCtx()
        {
            EVP_CIPHER_CTX* raw = EVP_CIPHER_CTX_new();
            return EvpCipherCtxPtr(raw, &EVP_CIPHER_CTX_free);
        }

    } // namespace

    bool Crypto::s_initialized = false;

    bool Crypto::Init()
    {
        std::lock_guard<std::mutex> lk(g_cryptoInitMutex);
        if (s_initialized) {
            return true;
        }
        s_initialized = true;
        return true;
    }

    void Crypto::Cleanup()
    {
        std::lock_guard<std::mutex> lk(g_cryptoInitMutex);
        s_initialized = false;
    }

    bool Crypto::EncryptMessage(
        const uint8_t* plaintext,
        size_t         plaintextLen,
        const uint8_t* key,
        size_t         keyLen,
        uint8_t*       nonce,
        size_t         nonceLen,
        std::vector<uint8_t>& ciphertext,
        uint8_t*       authTag,
        size_t         authTagLen)
    {
        if (!s_initialized || key == nullptr || nonce == nullptr || authTag == nullptr) {
            return false;
        }
        if (keyLen != ZChatIM::CRYPTO_KEY_SIZE || nonceLen != ZChatIM::NONCE_SIZE
            || authTagLen != ZChatIM::AUTH_TAG_SIZE) {
            return false;
        }
        if (plaintextLen > 0 && plaintext == nullptr) {
            return false;
        }

        if (plaintextLen > static_cast<size_t>(std::numeric_limits<int>::max())) {
            return false;
        }

        EvpCipherCtxPtr ctx = MakeCipherCtx();
        if (!ctx) {
            return false;
        }
        if (EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) {
            return false;
        }
        if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(nonceLen), nullptr) != 1) {
            return false;
        }
        if (EVP_EncryptInit_ex(ctx.get(), nullptr, nullptr, key, nonce) != 1) {
            return false;
        }

        ciphertext.resize(plaintextLen);
        int len  = 0;
        int len2 = 0;
        if (plaintextLen > 0) {
            if (EVP_EncryptUpdate(
                    ctx.get(),
                    ciphertext.data(),
                    &len,
                    plaintext,
                    static_cast<int>(plaintextLen))
                != 1) {
                ciphertext.clear();
                return false;
            }
        }
        if (EVP_EncryptFinal_ex(ctx.get(), ciphertext.data() + static_cast<size_t>(len), &len2) != 1) {
            ciphertext.clear();
            return false;
        }
        if (static_cast<size_t>(len) + static_cast<size_t>(len2) != plaintextLen) {
            ciphertext.clear();
            return false;
        }
        if (EVP_CIPHER_CTX_ctrl(
                ctx.get(),
                EVP_CTRL_GCM_GET_TAG,
                static_cast<int>(authTagLen),
                authTag)
            != 1) {
            ciphertext.clear();
            return false;
        }
        return true;
    }

    bool Crypto::DecryptMessage(
        const uint8_t* ciphertext,
        size_t         ciphertextLen,
        const uint8_t* key,
        size_t         keyLen,
        const uint8_t* nonce,
        size_t         nonceLen,
        const uint8_t* authTag,
        size_t         authTagLen,
        std::vector<uint8_t>& plaintext)
    {
        if (!s_initialized || key == nullptr || nonce == nullptr || authTag == nullptr) {
            return false;
        }
        if (keyLen != ZChatIM::CRYPTO_KEY_SIZE || nonceLen != ZChatIM::NONCE_SIZE
            || authTagLen != ZChatIM::AUTH_TAG_SIZE) {
            return false;
        }
        if (ciphertextLen > 0 && ciphertext == nullptr) {
            return false;
        }

        if (ciphertextLen > static_cast<size_t>(std::numeric_limits<int>::max())) {
            return false;
        }

        EvpCipherCtxPtr ctx = MakeCipherCtx();
        if (!ctx) {
            return false;
        }
        if (EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) {
            return false;
        }
        if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(nonceLen), nullptr) != 1) {
            return false;
        }
        if (EVP_DecryptInit_ex(ctx.get(), nullptr, nullptr, key, nonce) != 1) {
            return false;
        }

        plaintext.resize(ciphertextLen);
        int len  = 0;
        int len2 = 0;
        if (ciphertextLen > 0) {
            if (EVP_DecryptUpdate(
                    ctx.get(),
                    plaintext.data(),
                    &len,
                    ciphertext,
                    static_cast<int>(ciphertextLen))
                != 1) {
                plaintext.clear();
                return false;
            }
        }
        if (EVP_CIPHER_CTX_ctrl(
                ctx.get(),
                EVP_CTRL_GCM_SET_TAG,
                static_cast<int>(authTagLen),
                const_cast<uint8_t*>(authTag))
            != 1) {
            plaintext.clear();
            return false;
        }
        if (EVP_DecryptFinal_ex(ctx.get(), plaintext.data() + static_cast<size_t>(len), &len2) != 1) {
            plaintext.clear();
            return false;
        }
        if (static_cast<size_t>(len) + static_cast<size_t>(len2) != ciphertextLen) {
            plaintext.clear();
            return false;
        }
        return true;
    }

    std::vector<uint8_t> Crypto::GenerateKey(size_t keyLen)
    {
        return GenerateSecureRandom(keyLen);
    }

    std::vector<uint8_t> Crypto::GenerateNonce(size_t nonceLen)
    {
        return GenerateSecureRandom(nonceLen);
    }

    bool Crypto::DeriveKeyPbkdf2HmacSha256(
        const uint8_t* password,
        size_t         passwordLen,
        const uint8_t* salt,
        size_t         saltLen,
        int            iterations,
        uint8_t*       outputKey,
        size_t         outputKeyLen)
    {
        if (!s_initialized || password == nullptr || salt == nullptr || outputKey == nullptr) {
            return false;
        }
        if (passwordLen == 0 || saltLen == 0 || outputKeyLen == 0 || iterations < 10000) {
            return false;
        }
        if (passwordLen > static_cast<size_t>(std::numeric_limits<int>::max())
            || saltLen > static_cast<size_t>(std::numeric_limits<int>::max())
            || outputKeyLen > static_cast<size_t>(std::numeric_limits<int>::max())
            || iterations > std::numeric_limits<int>::max()) {
            return false;
        }
        return PKCS5_PBKDF2_HMAC(
                   reinterpret_cast<const char*>(password),
                   static_cast<int>(passwordLen),
                   salt,
                   static_cast<int>(saltLen),
                   iterations,
                   EVP_sha256(),
                   static_cast<int>(outputKeyLen),
                   outputKey)
               == 1;
    }

    bool Crypto::DeriveKey(
        const uint8_t* inputKey,
        size_t         inputKeyLen,
        const uint8_t* salt,
        size_t         saltLen,
        uint8_t*       outputKey,
        size_t         outputKeyLen)
    {
        constexpr int kIterations = 100000;
        return DeriveKeyPbkdf2HmacSha256(inputKey, inputKeyLen, salt, saltLen, kIterations, outputKey, outputKeyLen);
    }

    bool Crypto::HashSha256(const uint8_t* data, size_t dataLen, uint8_t* hash)
    {
        return crypto::Sha256(data, dataLen, hash);
    }

    std::vector<uint8_t> Crypto::HashSha256(const uint8_t* data, size_t dataLen)
    {
        std::vector<uint8_t> out(ZChatIM::SHA256_SIZE);
        if (!crypto::Sha256(data, dataLen, out.data())) {
            return {};
        }
        return out;
    }

    bool Crypto::CalculateMessageIdHash(const uint8_t* messageId, size_t messageIdLen, uint8_t* hash)
    {
        return crypto::Sha256(messageId, messageIdLen, hash);
    }

    std::vector<uint8_t> Crypto::GenerateSecureRandom(size_t length)
    {
        std::vector<uint8_t> out(length);
        if (length == 0) {
            return out;
        }
        if (length > static_cast<size_t>(std::numeric_limits<int>::max())) {
            return {};
        }
        if (RAND_bytes(out.data(), static_cast<int>(length)) == 1) {
            return out;
        }
        RAND_poll();
        if (RAND_bytes(out.data(), static_cast<int>(length)) == 1) {
            return out;
        }
        if (ReadDevUrandom(out.data(), length)) {
            return out;
        }
        return {};
    }

} // namespace ZChatIM::mm2
