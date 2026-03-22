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
            class KeyManagement {
            public:
                KeyManagement();
                ~KeyManagement();

                std::vector<uint8_t> GenerateMasterKey();

                std::vector<uint8_t> GenerateSessionKey();

                std::vector<uint8_t> GenerateMessageKey();

                std::vector<uint8_t> GenerateRandomKey(size_t keySize);

                std::vector<uint8_t> DeriveKey(const std::vector<uint8_t>& inputKey, const std::vector<uint8_t>& salt);

                std::vector<uint8_t> DeriveKey(const std::vector<uint8_t>& inputKey, const std::vector<uint8_t>& salt, size_t outputKeySize);

                bool StoreMasterKey(const std::vector<uint8_t>& key);

                std::vector<uint8_t> GetMasterKey();

                void ClearMasterKey();

                std::vector<uint8_t> RefreshMasterKey();

                std::vector<uint8_t> RefreshSessionKey();

                bool ValidateKey(const std::vector<uint8_t>& key);

                bool CheckKeyStrength(const std::vector<uint8_t>& key);

                bool ExportKey(const std::vector<uint8_t>& key, const std::string& filePath, const std::string& password);

                std::vector<uint8_t> ImportKey(const std::string& filePath, const std::string& password);

            private:
                KeyManagement(const KeyManagement&) = delete;
                KeyManagement& operator=(const KeyManagement&) = delete;

                bool IsValidKeySize(size_t keySize);
                bool IsStrongKey(const std::vector<uint8_t>& key);

                std::vector<uint8_t> m_masterKey;
            };

        } // namespace security
    } // namespace mm1
} // namespace ZChatIM
