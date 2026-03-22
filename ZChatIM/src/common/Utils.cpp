#include "common/Utils.h"

#include <cstring>
#include <limits>

namespace ZChatIM::common {

    std::string Utils::BytesToHex(const uint8_t* data, size_t length)
    {
        if (length > 0 && data == nullptr)
            return {};
        static const char* const kHex = "0123456789abcdef";
        std::string out;
        out.reserve(length * 2);
        for (size_t i = 0; i < length; ++i) {
            const uint8_t b = data[i];
            out.push_back(kHex[b >> 4]);
            out.push_back(kHex[b & 0x0F]);
        }
        return out;
    }

    bool Utils::HexToBytes(const std::string& hex, uint8_t* output, size_t outputLen)
    {
        if (hex.size() % 2 != 0)
            return false;
        const size_t need = hex.size() / 2;
        if (need > outputLen)
            return false;
        if (need > 0 && output == nullptr)
            return false;
        auto hexVal = [](char c) -> int {
            if (c >= '0' && c <= '9')
                return c - '0';
            if (c >= 'a' && c <= 'f')
                return c - 'a' + 10;
            if (c >= 'A' && c <= 'F')
                return c - 'A' + 10;
            return -1;
        };
        for (size_t i = 0; i < need; ++i) {
            const int hi = hexVal(hex[i * 2]);
            const int lo = hexVal(hex[i * 2 + 1]);
            if (hi < 0 || lo < 0)
                return false;
            output[i] = static_cast<uint8_t>((hi << 4) | lo);
        }
        return true;
    }

    std::vector<uint8_t> Utils::StringToBytes(const std::string& str)
    {
        return std::vector<uint8_t>(str.begin(), str.end());
    }

    std::string Utils::BytesToString(const uint8_t* data, size_t length)
    {
        if (length > 0 && data == nullptr)
            return {};
        std::string out;
        if (length > out.max_size())
            return {};
        return std::string(reinterpret_cast<const char*>(data), length);
    }

    uint32_t Utils::BigEndianToLittleEndian(uint32_t value)
    {
        return ((value & 0xFF000000u) >> 24) | ((value & 0x00FF0000u) >> 8) | ((value & 0x0000FF00u) << 8)
            | ((value & 0x000000FFu) << 24);
    }

    uint32_t Utils::LittleEndianToBigEndian(uint32_t value)
    {
        return BigEndianToLittleEndian(value);
    }

    uint64_t Utils::BigEndianToLittleEndian64(uint64_t value)
    {
        value = ((value & 0x00000000FFFFFFFFull) << 32) | ((value & 0xFFFFFFFF00000000ull) >> 32);
        value = ((value & 0x0000FFFF0000FFFFull) << 16) | ((value & 0xFFFF0000FFFF0000ull) >> 16);
        value = ((value & 0x00FF00FF00FF00FFull) << 8) | ((value & 0xFF00FF00FF00FF00ull) >> 8);
        return value;
    }

    uint64_t Utils::LittleEndianToBigEndian64(uint64_t value)
    {
        return BigEndianToLittleEndian64(value);
    }

    uint32_t Utils::CalculateCRC32(const uint8_t* data, size_t length)
    {
        if (length > 0 && data == nullptr)
            return 0;
        uint32_t crc = 0xFFFFFFFFu;
        for (size_t i = 0; i < length; ++i) {
            crc ^= data[i];
            for (int j = 0; j < 8; ++j)
                crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
        return ~crc;
    }

    uint32_t Utils::CalculateAdler32(const uint8_t* data, size_t length)
    {
        if (length > 0 && data == nullptr)
            return 0;
        constexpr uint32_t kMod = 65521;
        uint32_t a = 1;
        uint32_t b = 0;
        for (size_t i = 0; i < length; ++i) {
            a = (a + data[i]) % kMod;
            b = (b + a) % kMod;
        }
        return (b << 16) | a;
    }

} // namespace ZChatIM::common
