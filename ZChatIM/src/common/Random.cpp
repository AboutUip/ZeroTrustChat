#include "common/Random.h"
#include "common/OpenSsl3Required.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <random>

#include <openssl/rand.h>

#if !defined(_WIN32)
#    include <fcntl.h>
#    include <unistd.h>
#endif

namespace {

    // 防御异常 length 导致 vector/string 分配失控或 OOM（与 MM2 业务上限无关）。
    constexpr size_t kMaxRandomByteBlob    = 64ULL * 1024ULL * 1024ULL;
    constexpr size_t kMaxRandomStringChars = 16ULL * 1024ULL * 1024ULL;

} // namespace

namespace ZChatIM::common {

    namespace {

        constexpr char kAlpha62[] =
            "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
        constexpr size_t kAlpha62Count = sizeof(kAlpha62) - 1;

#if !defined(_WIN32)
        std::mutex g_urandomMutex;
        int g_urandomFd = -1;
#endif

    } // namespace

    static std::atomic<bool> g_rngSeeded{false};
    static std::mutex g_rngMutex;
    static std::mt19937 g_mt;

    static void EnsureInit()
    {
        if (g_rngSeeded.load(std::memory_order_acquire))
            return;
        std::lock_guard<std::mutex> lock(g_rngMutex);
        if (!g_rngSeeded.load(std::memory_order_relaxed)) {
            std::random_device rd;
            g_mt.seed(rd());
            g_rngSeeded.store(true, std::memory_order_release);
        }
    }

    void Random::Init()
    {
        EnsureInit();
    }

    static bool ReadOsRandom(void* dst, size_t len)
    {
        if (len > static_cast<size_t>((std::numeric_limits<int>::max)())) {
            return false;
        }
        auto* p = static_cast<uint8_t*>(dst);
        const int n = static_cast<int>(len);
        if (RAND_bytes(p, n) == 1) {
            return true;
        }
        RAND_poll();
        if (RAND_bytes(p, n) == 1) {
            return true;
        }
#if defined(_WIN32)
        return false;
#else
        std::lock_guard<std::mutex> lock(g_urandomMutex);
        if (g_urandomFd < 0) {
            g_urandomFd = ::open("/dev/urandom", O_RDONLY);
            if (g_urandomFd < 0)
                return false;
        }
        size_t off = 0;
        while (off < len) {
            const ssize_t n = ::read(g_urandomFd, p + off, len - off);
            if (n <= 0) {
                ::close(g_urandomFd);
                g_urandomFd = -1;
                return false;
            }
            off += static_cast<size_t>(n);
        }
        return true;
#endif
    }

    // Uniform index in [0, range) via OS RNG; rejection sampling avoids modulo bias.
    static bool ReadUniformIndex(uint64_t range, uint64_t* out)
    {
        if (!out || range == 0)
            return false;
        if (range == 1) {
            *out = 0;
            return true;
        }
        const uint64_t limit = UINT64_MAX - (UINT64_MAX % range);
        uint64_t u = 0;
        do {
            if (!ReadOsRandom(&u, sizeof(u)))
                return false;
        } while (u >= limit);
        *out = u % range;
        return true;
    }

    // 仅 **OpenSSL RAND**（+ Unix `/dev/urandom`）；失败则返回 false，**不**用 mt19937 冒充 CSPRNG。
    static bool FillStringUniformAlpha62(std::string& s)
    {
        for (size_t i = 0; i < s.size(); ++i) {
            uint64_t idx = 0;
            if (!ReadUniformIndex(kAlpha62Count, &idx)) {
                return false;
            }
            s[i] = kAlpha62[idx];
        }
        return true;
    }

    std::vector<uint8_t> Random::GenerateBytes(size_t length)
    {
        EnsureInit();
        std::vector<uint8_t> out(length);
        std::uniform_int_distribution<int> dist(0, 255);
        std::lock_guard<std::mutex> lock(g_rngMutex);
        for (size_t i = 0; i < length; ++i)
            out[i] = static_cast<uint8_t>(dist(g_mt));
        return out;
    }

    int32_t Random::GenerateInt(int32_t min, int32_t max)
    {
        if (min > max)
            std::swap(min, max);
        EnsureInit();
        std::uniform_int_distribution<int32_t> dist(min, max);
        std::lock_guard<std::mutex> lock(g_rngMutex);
        return dist(g_mt);
    }

    uint32_t Random::GenerateUInt(uint32_t min, uint32_t max)
    {
        if (min > max)
            std::swap(min, max);
        EnsureInit();
        std::uniform_int_distribution<uint32_t> dist(min, max);
        std::lock_guard<std::mutex> lock(g_rngMutex);
        return dist(g_mt);
    }

    bool Random::GenerateBool()
    {
        return (GenerateUInt(0, 1) != 0);
    }

    std::vector<uint8_t> Random::GenerateSecureBytes(size_t length)
    {
        if (length > kMaxRandomByteBlob)
            return {};
        std::vector<uint8_t> out(length);
        if (!ReadOsRandom(out.data(), length)) {
            return {};
        }
        return out;
    }

    int32_t Random::GenerateSecureInt(int32_t min, int32_t max)
    {
        if (min > max)
            std::swap(min, max);
        const int64_t span = static_cast<int64_t>(max) - static_cast<int64_t>(min) + 1;
        if (span <= 0)
            return min;
        const uint64_t range = static_cast<uint64_t>(span);
        uint64_t idx = 0;
        if (!ReadUniformIndex(range, &idx)) {
            // RAND 不可用：不再回退 mt19937，避免误用「伪安全」整数。
            return min;
        }
        return static_cast<int32_t>(static_cast<int64_t>(min) + static_cast<int64_t>(idx));
    }

    uint32_t Random::GenerateSecureUInt(uint32_t min, uint32_t max)
    {
        if (min > max)
            std::swap(min, max);
        const uint64_t range = static_cast<uint64_t>(max) - static_cast<uint64_t>(min) + 1u;
        if (range == 0)
            return min;
        uint64_t idx = 0;
        if (!ReadUniformIndex(range, &idx)) {
            return min;
        }
        return min + static_cast<uint32_t>(idx);
    }

    std::vector<uint8_t> Random::GenerateMessageId()
    {
        return GenerateSecureBytes(16);
    }

    std::vector<uint8_t> Random::GenerateSessionId()
    {
        return GenerateSecureBytes(4);
    }

    std::string Random::GenerateFileId()
    {
        std::string s(8, '\0');
        if (!FillStringUniformAlpha62(s)) {
            return {};
        }
        return s;
    }

    std::string Random::GenerateRandomString(size_t length)
    {
        if (length > kMaxRandomStringChars)
            return {};
        std::string s(length, '\0');
        if (!FillStringUniformAlpha62(s)) {
            return {};
        }
        return s;
    }

} // namespace ZChatIM::common
