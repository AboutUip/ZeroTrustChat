// MM2 Crypto: wire format nonce(12) ‖ ciphertext ‖ tag(16); PBKDF2-HMAC-SHA256, 100000 iters.
// Windows: BCrypt (AES-GCM / PBKDF2 / RNG). Linux/macOS: OpenSSL 3 libcrypto.

#include "mm2/storage/Crypto.h"
#include "mm2/crypto/Sha256.h"

#include <cstring>
#include <cwchar>
#include <fstream>
#include <limits>
#include <memory>

#if defined(_WIN32)
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>
#    include <bcrypt.h>
#    include <wincrypt.h>
#else
#    include <openssl/evp.h>
#    include <openssl/opensslv.h>
#    include <openssl/rand.h>
#    if OPENSSL_VERSION_MAJOR < 3
#        error "ZChatIM (non-Windows) requires OpenSSL 3.x (libcrypto)."
#    endif
#endif

namespace ZChatIM::mm2 {

    namespace {

        bool ReadDevUrandom(void* buf, size_t len)
        {
#if defined(_WIN32)
            (void)buf;
            (void)len;
            return false;
#else
            std::ifstream f("/dev/urandom", std::ios::binary);
            return static_cast<bool>(f.read(static_cast<char*>(buf), static_cast<std::streamsize>(len)));
#endif
        }

#if defined(_WIN32)
        BCRYPT_ALG_HANDLE g_pbkdf2Alg = nullptr;
        BCRYPT_ALG_HANDLE g_aesGcmAlg = nullptr;
        BCRYPT_ALG_HANDLE g_rngAlg = nullptr;

        bool FillRandomCryptGenRandom(uint8_t* buf, size_t length)
        {
            if (buf == nullptr || length == 0) {
                return false;
            }
            if (length > static_cast<size_t>(DWORD(-1))) {
                return false;
            }
            HCRYPTPROV hProv = 0;
            if (!CryptAcquireContextW(&hProv, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
#    if defined(PROV_RSA_AES)
                if (!CryptAcquireContextW(&hProv, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
                    return false;
                }
#    else
                return false;
#    endif
            }
            const BOOL ok = CryptGenRandom(hProv, static_cast<DWORD>(length), buf);
            CryptReleaseContext(hProv, 0);
            return ok != FALSE;
        }
#else
        using EvpCipherCtxPtr = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;

        EvpCipherCtxPtr MakeCipherCtx()
        {
            EVP_CIPHER_CTX* raw = EVP_CIPHER_CTX_new();
            return EvpCipherCtxPtr(raw, &EVP_CIPHER_CTX_free);
        }
#endif

    } // namespace

    bool Crypto::s_initialized = false;

    bool Crypto::Init()
    {
        if (s_initialized) {
            return true;
        }
#if defined(_WIN32)
        NTSTATUS st = BCryptOpenAlgorithmProvider(&g_aesGcmAlg, BCRYPT_AES_ALGORITHM, nullptr, 0);
        if (st != 0) {
            return false;
        }
        wchar_t gcmModeBuf[64];
        {
            const size_t n = std::wcslen(BCRYPT_CHAIN_MODE_GCM);
            if (n + 1U > 64) {
                BCryptCloseAlgorithmProvider(g_aesGcmAlg, 0);
                g_aesGcmAlg = nullptr;
                return false;
            }
            std::wmemcpy(gcmModeBuf, BCRYPT_CHAIN_MODE_GCM, n + 1U);
        }
        const ULONG modeBytes = static_cast<ULONG>((std::wcslen(gcmModeBuf) + 1U) * sizeof(wchar_t));
        st                    = BCryptSetProperty(
            g_aesGcmAlg,
            BCRYPT_CHAINING_MODE,
            reinterpret_cast<PUCHAR>(gcmModeBuf),
            modeBytes,
            0);
        if (st != 0) {
            BCryptCloseAlgorithmProvider(g_aesGcmAlg, 0);
            g_aesGcmAlg = nullptr;
            return false;
        }
        st = BCryptOpenAlgorithmProvider(&g_pbkdf2Alg, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG);
        if (st != 0) {
            BCryptCloseAlgorithmProvider(g_aesGcmAlg, 0);
            g_aesGcmAlg = nullptr;
            return false;
        }
        st = BCryptOpenAlgorithmProvider(&g_rngAlg, BCRYPT_RNG_ALGORITHM, nullptr, 0);
        if (st != 0) {
            st = BCryptOpenAlgorithmProvider(&g_rngAlg, BCRYPT_RNG_ALGORITHM, MS_PRIMITIVE_PROVIDER, 0);
        }
        if (st != 0) {
            g_rngAlg = nullptr;
        }
#else
        if (OPENSSL_VERSION_MAJOR < 3) {
            return false;
        }
#endif
        s_initialized = true;
        return true;
    }

    void Crypto::Cleanup()
    {
#if defined(_WIN32)
        if (g_rngAlg != nullptr) {
            BCryptCloseAlgorithmProvider(g_rngAlg, 0);
            g_rngAlg = nullptr;
        }
        if (g_pbkdf2Alg != nullptr) {
            BCryptCloseAlgorithmProvider(g_pbkdf2Alg, 0);
            g_pbkdf2Alg = nullptr;
        }
        if (g_aesGcmAlg != nullptr) {
            BCryptCloseAlgorithmProvider(g_aesGcmAlg, 0);
            g_aesGcmAlg = nullptr;
        }
#endif
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

#if defined(_WIN32)
        if (g_aesGcmAlg == nullptr) {
            return false;
        }

        BCRYPT_KEY_HANDLE hKey = nullptr;
        NTSTATUS          st   = BCryptGenerateSymmetricKey(
            g_aesGcmAlg, &hKey, nullptr, 0, const_cast<PUCHAR>(key), static_cast<ULONG>(keyLen), 0);
        if (st != 0) {
            return false;
        }

        const ULONG plainUL = static_cast<ULONG>(plaintextLen);
        constexpr ULONG kAesBlock = 16;
        const ULONG     outCap    = plainUL == 0 ? kAesBlock : ((plainUL + kAesBlock - 1U) / kAesBlock) * kAesBlock;
        ciphertext.resize(static_cast<size_t>(outCap));

        BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO auth{};
        BCRYPT_INIT_AUTH_MODE_INFO(auth);
        auth.pbNonce       = nonce;
        auth.cbNonce       = static_cast<ULONG>(nonceLen);
        auth.pbTag         = authTag;
        auth.cbTag         = static_cast<ULONG>(authTagLen);

        ULONG cbCipher = 0;
        st             = BCryptEncrypt(
            hKey,
            plainUL > 0 ? const_cast<PUCHAR>(plaintext) : nullptr,
            plainUL,
            &auth,
            nullptr,
            0,
            reinterpret_cast<PUCHAR>(ciphertext.data()),
            outCap,
            &cbCipher,
            0);
        BCryptDestroyKey(hKey);
        if (st != 0) {
            ciphertext.clear();
            return false;
        }
        if (cbCipher != plainUL) {
            ciphertext.clear();
            return false;
        }
        ciphertext.resize(static_cast<size_t>(cbCipher));
        return true;
#else
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
#endif
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

#if defined(_WIN32)
        if (g_aesGcmAlg == nullptr) {
            return false;
        }

        BCRYPT_KEY_HANDLE hKey = nullptr;
        NTSTATUS          st   = BCryptGenerateSymmetricKey(
            g_aesGcmAlg, &hKey, nullptr, 0, const_cast<PUCHAR>(key), static_cast<ULONG>(keyLen), 0);
        if (st != 0) {
            return false;
        }

        const ULONG ctUL = static_cast<ULONG>(ciphertextLen);
        constexpr ULONG kAesBlock = 16;
        const ULONG     outCap    = ctUL == 0 ? kAesBlock : ((ctUL + kAesBlock - 1U) / kAesBlock) * kAesBlock;
        plaintext.resize(static_cast<size_t>(outCap));

        BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO auth{};
        BCRYPT_INIT_AUTH_MODE_INFO(auth);
        auth.pbNonce       = const_cast<PUCHAR>(nonce);
        auth.cbNonce       = static_cast<ULONG>(nonceLen);
        auth.pbTag         = const_cast<PUCHAR>(authTag);
        auth.cbTag         = static_cast<ULONG>(authTagLen);

        ULONG cbPlain = 0;
        st            = BCryptDecrypt(
            hKey,
            ctUL > 0 ? const_cast<PUCHAR>(ciphertext) : nullptr,
            ctUL,
            &auth,
            nullptr,
            0,
            reinterpret_cast<PUCHAR>(plaintext.data()),
            outCap,
            &cbPlain,
            0);
        BCryptDestroyKey(hKey);
        if (st != 0) {
            plaintext.clear();
            return false;
        }
        if (cbPlain != ctUL) {
            plaintext.clear();
            return false;
        }
        plaintext.resize(static_cast<size_t>(cbPlain));
        return true;
#else
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
#endif
    }

    std::vector<uint8_t> Crypto::GenerateKey(size_t keyLen)
    {
        return GenerateSecureRandom(keyLen);
    }

    std::vector<uint8_t> Crypto::GenerateNonce(size_t nonceLen)
    {
        return GenerateSecureRandom(nonceLen);
    }

    bool Crypto::DeriveKey(
        const uint8_t* inputKey,
        size_t         inputKeyLen,
        const uint8_t* salt,
        size_t         saltLen,
        uint8_t*       outputKey,
        size_t         outputKeyLen)
    {
        if (!s_initialized || inputKey == nullptr || salt == nullptr || outputKey == nullptr) {
            return false;
        }
        if (inputKeyLen == 0 || saltLen == 0 || outputKeyLen == 0) {
            return false;
        }
#if defined(_WIN32)
        if (g_pbkdf2Alg == nullptr) {
            return false;
        }
        const NTSTATUS st = BCryptDeriveKeyPBKDF2(
            g_pbkdf2Alg,
            const_cast<PUCHAR>(inputKey),
            static_cast<ULONG>(inputKeyLen),
            const_cast<PUCHAR>(salt),
            static_cast<ULONG>(saltLen),
            100000ULL,
            outputKey,
            static_cast<ULONG>(outputKeyLen),
            0);
        return st == 0;
#else
        if (inputKeyLen > static_cast<size_t>(std::numeric_limits<int>::max())
            || saltLen > static_cast<size_t>(std::numeric_limits<int>::max())
            || outputKeyLen > static_cast<size_t>(std::numeric_limits<int>::max())) {
            return false;
        }
        constexpr int kIterations = 100000;
        return PKCS5_PBKDF2_HMAC(
                   reinterpret_cast<const char*>(inputKey),
                   static_cast<int>(inputKeyLen),
                   salt,
                   static_cast<int>(saltLen),
                   kIterations,
                   EVP_sha256(),
                   static_cast<int>(outputKeyLen),
                   outputKey)
               == 1;
#endif
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
#if defined(_WIN32)
        if (length > static_cast<size_t>(std::numeric_limits<ULONG>::max())) {
            return {};
        }
        // BCryptGenRandom 返回 NTSTATUS，成功为 0；勿用 BOOL 接收（0 会被当成 false）。
        if (g_rngAlg != nullptr) {
            const NTSTATUS stRng = BCryptGenRandom(g_rngAlg, out.data(), static_cast<ULONG>(length), 0);
            if (stRng == 0) {
                return out;
            }
        }
        const NTSTATUS stFb =
            BCryptGenRandom(nullptr, out.data(), static_cast<ULONG>(length), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        if (stFb == 0) {
            return out;
        }
        if (FillRandomCryptGenRandom(out.data(), length)) {
            return out;
        }
        return {};
#else
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
#endif
    }

} // namespace ZChatIM::mm2
