#pragma once

#include <vector>
#include <cstdint>

namespace ZChatIM {
    namespace mm1 {
        class FriendNoteManager {
        public:
            bool UpdateFriendNote(
                const std::vector<uint8_t>& userId,
                const std::vector<uint8_t>& friendId,
                const std::vector<uint8_t>& newEncryptedNote,
                uint64_t updateTimestampSeconds,
                const std::vector<uint8_t>& signatureEd25519);
        };
    } // namespace mm1
} // namespace ZChatIM
