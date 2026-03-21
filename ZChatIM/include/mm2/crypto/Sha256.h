#pragma once

#include <cstddef>
#include <cstdint>

namespace ZChatIM::crypto {

    // SHA-256 over arbitrary bytes. Returns false on failure (e.g. BCrypt error on Windows).
    bool Sha256(const uint8_t* data, size_t length, uint8_t outDigest[32]);

} // namespace ZChatIM::crypto
