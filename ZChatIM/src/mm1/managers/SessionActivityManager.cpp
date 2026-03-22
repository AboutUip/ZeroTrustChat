#include "mm1/managers/SessionActivityManager.h"
#include "mm2/MM2.h"
#include "Types.h"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <vector>

namespace ZChatIM::mm1 {
    namespace {

        constexpr uint64_t kIdleTimeoutMs = 30ULL * 60ULL * 1000ULL;

        constexpr size_t kImSessionIdBytes = USER_ID_SIZE;

        uint64_t NowUnixEpochMs()
        {
            using namespace std::chrono;
            return static_cast<uint64_t>(
                duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
        }

        uint64_t SanitizeNowMs(uint64_t clientMs)
        {
            const uint64_t srv = NowUnixEpochMs();
            constexpr uint64_t kMaxFutureSkewMs = 120000;
            constexpr uint64_t kMaxPastSkewMs   = 600000;
            if (clientMs > srv) {
                if (clientMs - srv > kMaxFutureSkewMs) {
                    return srv;
                }
            } else if (srv > clientMs) {
                if (srv - clientMs > kMaxPastSkewMs) {
                    return srv;
                }
            }
            return clientMs;
        }

    } // namespace

    struct SessionActivityManager::Impl {
        // 占位：保留 unique_ptr 布局，逻辑均在 MM2 / SQLite。
    };

    SessionActivityManager::SessionActivityManager()
        : impl_(std::make_unique<Impl>())
    {
    }

    SessionActivityManager::~SessionActivityManager() = default;

    SessionActivityManager::SessionActivityManager(SessionActivityManager&&) noexcept = default;

    SessionActivityManager& SessionActivityManager::operator=(SessionActivityManager&&) noexcept = default;

    void SessionActivityManager::TouchSession(const std::vector<uint8_t>& sessionId, uint64_t nowMs)
    {
        if (!impl_ || sessionId.size() != kImSessionIdBytes) {
            return;
        }

        const uint64_t t = SanitizeNowMs(nowMs);
        mm2::MM2&      m2 = mm2::MM2::Instance();
        (void)m2.Mm1CleanupExpiredImSessionActivity(t, kIdleTimeoutMs);
        (void)m2.Mm1TouchImSessionActivity(sessionId, t);
    }

    bool SessionActivityManager::IsSessionExpired(
        const std::vector<uint8_t>& sessionId,
        uint64_t nowMs) const
    {
        if (!impl_ || sessionId.size() != kImSessionIdBytes) {
            return true;
        }

        const uint64_t t = SanitizeNowMs(nowMs);

        mm2::MM2& m2 = mm2::MM2::Instance();
        uint64_t  last = 0;
        bool      found = false;
        if (!m2.Mm1SelectImSessionLastActive(sessionId, last, found)) {
            return true;
        }
        if (!found) {
            return true;
        }
        if (t < last) {
            return false;
        }
        return (t - last) > kIdleTimeoutMs;
    }

    bool SessionActivityManager::GetSessionStatus(const std::vector<uint8_t>& sessionId) const
    {
        return !IsSessionExpired(sessionId, NowUnixEpochMs());
    }

    void SessionActivityManager::CleanupExpiredSessions(uint64_t nowMs)
    {
        if (!impl_) {
            return;
        }

        const uint64_t t = SanitizeNowMs(nowMs);
        (void)mm2::MM2::Instance().Mm1CleanupExpiredImSessionActivity(t, kIdleTimeoutMs);
    }

    void SessionActivityManager::ClearAllTrackedSessions()
    {
        if (!impl_) {
            return;
        }
        (void)mm2::MM2::Instance().Mm1ClearAllImSessionActivity();
    }

} // namespace ZChatIM::mm1
