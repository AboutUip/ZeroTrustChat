#pragma once

#include "Types.h"
#include <vector>
#include <string>

namespace ZChatIM
{
    namespace mm2
    {
        class MessageBlock {
        public:
            MessageBlock();
            MessageBlock(const std::vector<uint8_t>& messageId, const std::vector<uint8_t>& payload);

            bool Construct(
                const std::vector<uint8_t>& messageId,
                const std::vector<uint8_t>& payload,
                const std::vector<uint8_t>& senderId,
                uint64_t sequence,
                const std::vector<uint8_t>& prevHash
            );

            bool Serialize(std::vector<uint8_t>& output);

            bool Deserialize(const std::vector<uint8_t>& input);

            bool Validate() const;

            std::vector<uint8_t> GetMessageId() const;

            std::vector<uint8_t> GetPayload() const;

            std::vector<uint8_t> GetSenderId() const;

            uint64_t GetSequence() const;

            uint64_t GetTimestamp() const;

            std::vector<uint8_t> GetPrevHash() const;

            size_t GetSize() const;

            static size_t CalculateSize(size_t payloadSize);

            static bool ValidateHeader(const BlockHead& head);

        private:
            bool GenerateIdHash(const std::vector<uint8_t>& messageId);

            bool EncryptContent();

            bool DecryptContent();

            BlockHead m_head;
            uint8_t m_cryptoKey[CRYPTO_KEY_SIZE];
            uint8_t m_nonce[NONCE_SIZE];
            std::vector<uint8_t> m_content;
            uint8_t m_authTag[AUTH_TAG_SIZE];

            uint64_t m_sequence;
            uint64_t m_timestamp;
            uint8_t m_prevHash[SHA256_SIZE];
            uint8_t m_senderId[USER_ID_SIZE];
            std::vector<uint8_t> m_payload;
        };

    } // namespace mm2
} // namespace ZChatIM
