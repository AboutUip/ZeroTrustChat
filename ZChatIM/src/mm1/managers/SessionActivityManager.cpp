#include "mm1/managers/SessionActivityManager.h"
#include "Types.h"

#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <vector>

namespace ZChatIM::mm1 {
    namespace {

        // docs/03-Business/04-Session.md：idle 超时 30 分钟
        constexpr uint64_t kIdleTimeoutMs = 30ULL * 60ULL * 1000ULL;

        constexpr size_t kImSessionIdBytes = USER_ID_SIZE;

        // 防止不可信侧刷爆内存；与 AuthSessionManager 同量级策略。
        constexpr size_t kMaxTrackedSessions = 100000;

        uint64_t NowUnixEpochMs()
        {
            using namespace std::chrono;
            return static_cast<uint64_t>(
                duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
        }

        // 钳制不可信 nowMs：禁止大幅超前「刷新」idle，禁止过久回溯导致误清理他人会话。
        // 用差值比较，避免 srv + skew 的 uint64 溢出。
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

        void EraseIdleExpired(
            std::map<std::vector<uint8_t>, uint64_t>& lastActiveMs,
            uint64_t nowMs,
            uint64_t idleTimeoutMs)
        {
            for (auto it = lastActiveMs.begin(); it != lastActiveMs.end();) {
                if (nowMs >= it->second && (nowMs - it->second) > idleTimeoutMs) {
                    it = lastActiveMs.erase(it);
                } else {
                    ++it;
                }
            }
        }

    } // namespace

    struct SessionActivityManager::Impl {
        mutable std::mutex mutex;
        std::map<std::vector<uint8_t>, uint64_t> lastActiveMs;
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

        std::lock_guard<std::mutex> lock(impl_->mutex);
        EraseIdleExpired(impl_->lastActiveMs, t, kIdleTimeoutMs);

        const auto existing = impl_->lastActiveMs.find(sessionId);
        if (existing == impl_->lastActiveMs.end()
            && impl_->lastActiveMs.size() >= kMaxTrackedSessions) {
            return;
        }

        impl_->lastActiveMs[sessionId] = t;
    }

    bool SessionActivityManager::IsSessionExpired(
        const std::vector<uint8_t>& sessionId,
        uint64_t nowMs) const
    {
        if (!impl_ || sessionId.size() != kImSessionIdBytes) {
            return true;
        }

        const uint64_t t = SanitizeNowMs(nowMs);

        std::lock_guard<std::mutex> lock(impl_->mutex);
        const auto it = impl_->lastActiveMs.find(sessionId);
        if (it == impl_->lastActiveMs.end()) {
            return true;
        }
        if (t < it->second) {
            return false;
        }
        return (t - it->second) > kIdleTimeoutMs;
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

        std::lock_guard<std::mutex> lock(impl_->mutex);
        EraseIdleExpired(impl_->lastActiveMs, t, kIdleTimeoutMs);
    }

} // namespace ZChatIM::mm1
