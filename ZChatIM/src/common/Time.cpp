#include "common/Time.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <limits>
#include <sstream>

namespace ZChatIM::common {

    namespace {

        uint64_t NowMs()
        {
            using clock = std::chrono::system_clock;
            return static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(clock::now().time_since_epoch()).count());
        }

        uint64_t NowSec()
        {
            using clock = std::chrono::system_clock;
            return static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::seconds>(clock::now().time_since_epoch()).count());
        }

    } // namespace

    uint64_t Time::GetCurrentTimestamp()
    {
        return NowMs();
    }

    uint64_t Time::GetCurrentTimestampSeconds()
    {
        return NowSec();
    }

    std::string Time::TimestampToString(uint64_t timestamp)
    {
        return std::to_string(timestamp);
    }

    uint64_t Time::StringToTimestamp(const std::string& timeStr)
    {
        try {
            return std::stoull(timeStr);
        } catch (...) {
            return 0;
        }
    }

    int64_t Time::CalculateDuration(uint64_t start, uint64_t end)
    {
        return static_cast<int64_t>(end) - static_cast<int64_t>(start);
    }

    int64_t Time::CalculateDurationSeconds(uint64_t start, uint64_t end)
    {
        const int64_t ms = CalculateDuration(start, end);
        return ms / 1000;
    }

    bool Time::IsExpired(uint64_t timestamp, uint64_t durationMs)
    {
        const uint64_t now = NowMs();
        if (now < timestamp)
            return false;
        return (now - timestamp) > durationMs;
    }

    bool Time::IsExpiredSeconds(uint64_t timestamp, uint64_t durationSeconds)
    {
        const uint64_t now = NowSec();
        if (now < timestamp)
            return false;
        return (now - timestamp) > durationSeconds;
    }

    bool Time::IsExpiredDays(uint64_t timestamp, uint64_t days)
    {
        constexpr uint64_t kSecPerDay = 86400;
        constexpr uint64_t kMaxDays = std::numeric_limits<uint64_t>::max() / kSecPerDay;
        if (days > kMaxDays)
            return false;
        return IsExpiredSeconds(timestamp, days * kSecPerDay);
    }

    std::string Time::GetCurrentTimeString()
    {
        return TimestampToString(GetCurrentTimestamp());
    }

    std::string Time::GetCurrentDateString()
    {
        using clock = std::chrono::system_clock;
        const std::time_t t = clock::to_time_t(clock::now());
        std::tm tmBuf{};
#if defined(_WIN32)
        if (gmtime_s(&tmBuf, &t) != 0)
            return {};
#else
        if (gmtime_r(&t, &tmBuf) == nullptr)
            return {};
#endif
        std::ostringstream oss;
        oss << std::put_time(&tmBuf, "%Y-%m-%d");
        return oss.str();
    }

    std::string Time::GetCurrentDateTimeString()
    {
        using clock = std::chrono::system_clock;
        const std::time_t t = clock::to_time_t(clock::now());
        std::tm tmBuf{};
#if defined(_WIN32)
        if (gmtime_s(&tmBuf, &t) != 0)
            return {};
#else
        if (gmtime_r(&t, &tmBuf) == nullptr)
            return {};
#endif
        std::ostringstream oss;
        oss << std::put_time(&tmBuf, "%Y-%m-%d %H:%M:%S");
        return oss.str();
    }

} // namespace ZChatIM::common
