#pragma once

#include "../Types.h"
#include <vector>
#include <string>

namespace ZChatIM
{
    namespace mm1
    {
        namespace security
        {
            class SecureRandom {
            public:
                SecureRandom();
                ~SecureRandom();

                std::vector<uint8_t> Generate(size_t size);

                bool Generate(uint8_t* buffer, size_t size);

                int32_t GenerateInt(int32_t min, int32_t max);

                uint32_t GenerateUInt(uint32_t min, uint32_t max);

                // mt19937_64 seeded once from mm2::Crypto::GenerateSecureRandom(8); not per-call RAND.
                int64_t GenerateInt64(int64_t min, int64_t max);

                uint64_t GenerateUInt64(uint64_t min, uint64_t max);

                bool GenerateBool();

                std::vector<uint8_t> GenerateMessageId();

                std::vector<uint8_t> GenerateSessionId();

                std::string GenerateFileId();

                std::string GenerateRandomString(size_t length);

                bool CheckQuality();

                double GetEntropy();

                bool Initialize();

                void Cleanup();

                bool IsInitialized();

            private:
                SecureRandom(const SecureRandom&) = delete;
                SecureRandom& operator=(const SecureRandom&) = delete;

                bool InitSystemRandom();
                bool InitHardwareRandom();

                bool m_initialized;
            };

        } // namespace security
    } // namespace mm1
} // namespace ZChatIM
