#include "mm1/managers/AuthSessionManager.h"
#include "Types.h"

#include <array>
#include <ctime>
#include <chrono>
#include <cstring>
#include <deque>
#include <limits>
#include <map>
#include <mutex>
#include <vector>

#if !defined(_WIN32)
#include <cerrno>
#endif

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace ZChatIM::mm1 {
    namespace {

        constexpr size_t kSessionIdBytes = JNI_AUTH_SESSION_TOKEN_BYTES;
        constexpr size_t kMaxTokenBytes  = 8192;
        constexpr size_t kMinTokenBytes  = 1;

        // 单调时钟 TTL（Auth 句柄）。与 04-Session idle、01-Overview 会话密钥周期不同，见文档分节。
        constexpr auto kDefaultSessionTtl = std::chrono::hours(24 * 7);

        constexpr size_t kMaxActiveSessions = 100000;

        // docs/03-Business/02-Auth.md
        constexpr uint64_t kUserAuthWindowMs   = 60ULL * 1000ULL;
        constexpr unsigned kUserAuthMaxPerMin  = 10;
        constexpr uint64_t kIpAuthWindowMs     = 60ULL * 1000ULL;
        constexpr unsigned kIpAuthMaxPerMin    = 5;
        constexpr uint64_t kBanFailWindowMs    = 24ULL * 60ULL * 60ULL * 1000ULL;

        struct SessionEntry {
            std::array<uint8_t, USER_ID_SIZE> userId{};
            std::chrono::steady_clock::time_point expiresAt{};
        };

        struct BanState {
            uint64_t bannedUntilMs = 0;
            std::deque<uint64_t> failTimesWithinWindow;
        };

        uint64_t SystemUnixEpochMs()
        {
            using namespace std::chrono;
            return static_cast<uint64_t>(
                duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
        }

        // 避免 (nowMs - t0) 在 uint64_t 下因 t0 > nowMs 回绕误判；队列头异常未来时间则丢弃。
        void PruneDequeOlderThan(std::deque<uint64_t>& d, uint64_t nowMs, uint64_t windowMs)
        {
            while (!d.empty()) {
                const uint64_t t0 = d.front();
                if (t0 <= nowMs) {
                    if (nowMs - t0 > windowMs) {
                        d.pop_front();
                        continue;
                    }
                } else {
                    d.pop_front();
                    continue;
                }
                break;
            }
        }

        // userId 固定 16B；非空 ip 直接拼接，与 02-Auth「userId + IP」一致且无歧义。
        std::vector<uint8_t> PrincipalKey(
            const std::vector<uint8_t>& userId,
            const std::vector<uint8_t>& clientIp)
        {
            if (clientIp.empty()) {
                return userId;
            }
            std::vector<uint8_t> k = userId;
            k.insert(k.end(), clientIp.begin(), clientIp.end());
            return k;
        }

        // 封禁矩阵：连续失败次数 n（含本次）→ 自 now 起禁多久。
        uint64_t BanDurationMsForFailureCount(size_t n)
        {
            if (n < 5) {
                return 0;
            }
            if (n == 5) {
                return 15ULL * 60ULL * 1000ULL;
            }
            if (n == 6) {
                return 30ULL * 60ULL * 1000ULL;
            }
            if (n == 7) {
                return 45ULL * 60ULL * 1000ULL;
            }
            if (n == 8) {
                return 60ULL * 60ULL * 1000ULL;
            }
            if (n == 9) {
                return 75ULL * 60ULL * 1000ULL;
            }
            if (n == 10) {
                return 90ULL * 60ULL * 1000ULL;
            }
            return 24ULL * 60ULL * 60ULL * 1000ULL;
        }

        void RecordAuthFailure(
            std::map<std::vector<uint8_t>, BanState>& banByPrincipal,
            const std::vector<uint8_t>& principalKey,
            uint64_t nowMs)
        {
            BanState& st = banByPrincipal[principalKey];
            PruneDequeOlderThan(st.failTimesWithinWindow, nowMs, kBanFailWindowMs);
            st.failTimesWithinWindow.push_back(nowMs);
            const size_t n = st.failTimesWithinWindow.size();
            if (n >= 5) {
                const uint64_t dur = BanDurationMsForFailureCount(n);
                uint64_t until    = nowMs + dur;
                if (until < nowMs) {
                    until = std::numeric_limits<uint64_t>::max();
                }
                st.bannedUntilMs = until;
            }
        }

        bool FillCryptoRandom(uint8_t* out, size_t len)
        {
            if (out == nullptr || len == 0) {
                return false;
            }
        #if defined(_WIN32)
            if (len > static_cast<size_t>(std::numeric_limits<ULONG>::max())) {
                return false;
            }
            const NTSTATUS st = BCryptGenRandom(
                nullptr,
                out,
                static_cast<ULONG>(len),
                BCRYPT_USE_SYSTEM_PREFERRED_RNG);
            return st == 0;
        #else
            const int fd = open("/dev/urandom", O_RDONLY);
            if (fd < 0) {
                return false;
            }
            size_t off = 0;
            while (off < len) {
                const ssize_t r = read(fd, out + off, len - off);
                if (r < 0) {
                    if (errno == EINTR) {
                        continue;
                    }
                    close(fd);
                    return false;
                }
                if (r == 0) {
                    close(fd);
                    return false;
                }
                off += static_cast<size_t>(r);
            }
            close(fd);
            return true;
        #endif
        }

        bool CredentialShapeOk(const std::vector<uint8_t>& token)
        {
            return token.size() >= kMinTokenBytes && token.size() <= kMaxTokenBytes;
        }

        // 占位：上线前须替换为真实凭证校验；返回 false 时会计入封禁失败次数。
        bool VerifyCredential(const std::vector<uint8_t>& userId, const std::vector<uint8_t>& token)
        {
            (void)userId;
            (void)token;
            return true;
        }

        bool GenerateUniqueSessionId(
            const std::map<std::vector<uint8_t>, SessionEntry>& map,
            std::vector<uint8_t>& out)
        {
            out.resize(kSessionIdBytes);
            for (int attempt = 0; attempt < 8; ++attempt) {
                if (!FillCryptoRandom(out.data(), out.size())) {
                    return false;
                }
                if (map.find(out) == map.end()) {
                    return true;
                }
            }
            return false;
        }

        void SecureZeroSessionEntry(SessionEntry& e)
        {
            volatile uint8_t* p = e.userId.data();
            for (size_t i = 0; i < e.userId.size(); ++i) {
                p[i] = 0;
            }
        }

    } // namespace

    struct AuthSessionManager::Impl {
        std::mutex mutex;
        std::map<std::vector<uint8_t>, SessionEntry> sessions;
        std::map<std::vector<uint8_t>, std::deque<uint64_t>> authAttemptsByUser;
        std::map<std::vector<uint8_t>, std::deque<uint64_t>> authAttemptsByIp;
        std::map<std::vector<uint8_t>, BanState> banByPrincipal;
    };

    AuthSessionManager::AuthSessionManager()
        : impl_(std::make_unique<Impl>())
    {
    }

    AuthSessionManager::~AuthSessionManager() = default;

    AuthSessionManager::AuthSessionManager(AuthSessionManager&&) noexcept = default;

    AuthSessionManager& AuthSessionManager::operator=(AuthSessionManager&&) noexcept = default;

    std::vector<uint8_t> AuthSessionManager::Auth(
        const std::vector<uint8_t>& userId,
        const std::vector<uint8_t>& token,
        const std::vector<uint8_t>& clientIp)
    {
        if (!impl_) {
            return {};
        }
        if (userId.size() != USER_ID_SIZE) {
            return {};
        }

        std::lock_guard<std::mutex> lock(impl_->mutex);

        const uint64_t nowMs = SystemUnixEpochMs();
        const std::vector<uint8_t> principalKey = PrincipalKey(userId, clientIp);

        const auto banIt = impl_->banByPrincipal.find(principalKey);
        if (banIt != impl_->banByPrincipal.end()) {
            PruneDequeOlderThan(banIt->second.failTimesWithinWindow, nowMs, kBanFailWindowMs);
            if (banIt->second.bannedUntilMs != 0 && nowMs >= banIt->second.bannedUntilMs) {
                banIt->second.bannedUntilMs = 0;
            }
            if (banIt->second.bannedUntilMs != 0 && nowMs < banIt->second.bannedUntilMs) {
                return {};
            }
        }

        std::deque<uint64_t>& userQ = impl_->authAttemptsByUser[userId];
        PruneDequeOlderThan(userQ, nowMs, kUserAuthWindowMs);
        if (userQ.size() >= kUserAuthMaxPerMin) {
            return {};
        }

        std::deque<uint64_t>* ipQPtr = nullptr;
        if (!clientIp.empty()) {
            std::deque<uint64_t>& ipQ = impl_->authAttemptsByIp[clientIp];
            ipQPtr = &ipQ;
            PruneDequeOlderThan(ipQ, nowMs, kIpAuthWindowMs);
            if (ipQ.size() >= kIpAuthMaxPerMin) {
                return {};
            }
        }

        // 本次请求计入限流窗口（含后续失败），与 02-Auth「次数/分钟」语义一致。
        userQ.push_back(nowMs);
        if (ipQPtr != nullptr) {
            ipQPtr->push_back(nowMs);
        }

        if (!CredentialShapeOk(token)) {
            RecordAuthFailure(impl_->banByPrincipal, principalKey, nowMs);
            return {};
        }

        if (!VerifyCredential(userId, token)) {
            RecordAuthFailure(impl_->banByPrincipal, principalKey, nowMs);
            return {};
        }

        if (impl_->sessions.size() >= kMaxActiveSessions) {
            // Credential already passed; do not consume rate-limit slots for server capacity issues.
            if (!userQ.empty()) {
                userQ.pop_back();
            }
            if (ipQPtr != nullptr && !ipQPtr->empty()) {
                ipQPtr->pop_back();
            }
            return {};
        }

        std::vector<uint8_t> sid;
        if (!GenerateUniqueSessionId(impl_->sessions, sid)) {
            if (!userQ.empty()) {
                userQ.pop_back();
            }
            if (ipQPtr != nullptr && !ipQPtr->empty()) {
                ipQPtr->pop_back();
            }
            return {};
        }

        SessionEntry entry{};
        std::memcpy(entry.userId.data(), userId.data(), USER_ID_SIZE);
        entry.expiresAt = std::chrono::steady_clock::now() + kDefaultSessionTtl;

        const std::vector<uint8_t> sessionIdCopy = sid;
        impl_->sessions.emplace(std::move(sid), std::move(entry));

        impl_->banByPrincipal.erase(principalKey);
        userQ.clear();
        if (ipQPtr != nullptr) {
            ipQPtr->clear();
        }

        return sessionIdCopy;
    }

    bool AuthSessionManager::VerifySession(const std::vector<uint8_t>& sessionId)
    {
        if (!impl_) {
            return false;
        }
        if (sessionId.size() != kSessionIdBytes) {
            return false;
        }

        std::lock_guard<std::mutex> lock(impl_->mutex);
        const auto it = impl_->sessions.find(sessionId);
        if (it == impl_->sessions.end()) {
            return false;
        }
        if (std::chrono::steady_clock::now() >= it->second.expiresAt) {
            SessionEntry dead = std::move(it->second);
            impl_->sessions.erase(it);
            SecureZeroSessionEntry(dead);
            return false;
        }
        return true;
    }

    bool AuthSessionManager::DestroySession(const std::vector<uint8_t>& sessionId)
    {
        if (!impl_) {
            return false;
        }
        if (sessionId.size() != kSessionIdBytes) {
            return false;
        }

        std::lock_guard<std::mutex> lock(impl_->mutex);
        const auto it = impl_->sessions.find(sessionId);
        if (it == impl_->sessions.end()) {
            return false;
        }
        SessionEntry dead = std::move(it->second);
        impl_->sessions.erase(it);
        SecureZeroSessionEntry(dead);
        return true;
    }

} // namespace ZChatIM::mm1
