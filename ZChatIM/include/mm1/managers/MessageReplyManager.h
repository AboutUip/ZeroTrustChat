#pragma once

#include <cstdint>
#include <vector>

namespace ZChatIM {
    namespace mm1 {
        // Ed25519 verify then MM2 reply map. Principal == senderId; canonical v1 fixed layout (see impl).
        class MessageReplyManager {
        public:
            bool StoreMessageReplyRelation(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& senderEd25519PublicKey,
                const std::vector<uint8_t>& messageId,
                const std::vector<uint8_t>& repliedMsgId,
                const std::vector<uint8_t>& repliedSenderId,
                const std::vector<uint8_t>& repliedContentDigest,
                const std::vector<uint8_t>& senderId,
                const std::vector<uint8_t>& signatureEd25519);
        };
    } // namespace mm1
} // namespace ZChatIM
