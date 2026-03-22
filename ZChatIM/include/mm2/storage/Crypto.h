#pragma once

#include "Types.h"
#include <vector>
#include <string>

namespace ZChatIM
{
    namespace mm2
    {
        // OpenSSL 3 GCM; wire format 12B nonce|ciphertext|16B tag. Pair Init/Cleanup with MM2.
        // Encrypt/Decrypt/DeriveKey need Init; RAND/HashSha256 do not.
        class Crypto {
        public:
            static bool Init();

            static void Cleanup();

            static bool EncryptMessage(
                const uint8_t* plaintext, size_t plaintextLen,
                const uint8_t* key, size_t keyLen,
                uint8_t* nonce, size_t nonceLen,
                std::vector<uint8_t>& ciphertext,
                uint8_t* authTag, size_t authTagLen
            );

            static bool DecryptMessage(
                const uint8_t* ciphertext, size_t ciphertextLen,
                const uint8_t* key, size_t keyLen,
                const uint8_t* nonce, size_t nonceLen,
                const uint8_t* authTag, size_t authTagLen,
                std::vector<uint8_t>& plaintext
            );

            static std::vector<uint8_t> GenerateKey(size_t keyLen = ZChatIM::CRYPTO_KEY_SIZE);

            static std::vector<uint8_t> GenerateNonce(size_t nonceLen = ZChatIM::NONCE_SIZE);

            static bool DeriveKey(
                const uint8_t* inputKey, size_t inputKeyLen,
                const uint8_t* salt, size_t saltLen,
                uint8_t* outputKey, size_t outputKeyLen
            );

            // ZMKP uses 200000 iters (MM2_message_key_passphrase.cpp).
            static bool DeriveKeyPbkdf2HmacSha256(
                const uint8_t* password,
                size_t         passwordLen,
                const uint8_t* salt,
                size_t         saltLen,
                int            iterations,
                uint8_t*       outputKey,
                size_t         outputKeyLen);

            static bool HashSha256(const uint8_t* data, size_t dataLen, uint8_t* hash);

            static std::vector<uint8_t> HashSha256(const uint8_t* data, size_t dataLen);

            static bool CalculateMessageIdHash(const uint8_t* messageId, size_t messageIdLen, uint8_t* hash);

            // RAND_bytes (+poll); Unix urandom fallback; failure => empty.
            static std::vector<uint8_t> GenerateSecureRandom(size_t length);
            
        private:
            Crypto() = delete;
            ~Crypto() = delete;

            static bool s_initialized;
        };
        
    } // namespace mm2
} // namespace ZChatIM
