#pragma once

#include "../Types.h"
#include <cstdint>

namespace ZChatIM
{
    namespace mm1
    {
        namespace security
        {
            class MemoryEncryption {
            public:
                MemoryEncryption();
                ~MemoryEncryption();

                bool Encrypt(void* ptr, size_t size, const uint8_t* key, size_t keySize);

                bool Decrypt(void* ptr, size_t size, const uint8_t* key, size_t keySize);

                bool EncryptBlock(void* ptr, size_t size, const uint8_t* key);

                bool DecryptBlock(void* ptr, size_t size, const uint8_t* key);

                bool GenerateKey(uint8_t* key, size_t keySize);

                bool DeriveKey(const uint8_t* inputKey, size_t inputKeySize, uint8_t* outputKey, size_t outputKeySize);

                bool IsValidKeySize(size_t keySize);

                size_t GetRecommendedKeySize();

            private:
                MemoryEncryption(const MemoryEncryption&) = delete;
                MemoryEncryption& operator=(const MemoryEncryption&) = delete;

                bool EncryptXor(void* ptr, size_t size, const uint8_t* key, size_t keySize);
                bool DecryptXor(void* ptr, size_t size, const uint8_t* key, size_t keySize);
            };

        } // namespace security
    } // namespace mm1
} // namespace ZChatIM
