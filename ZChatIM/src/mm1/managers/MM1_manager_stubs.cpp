// MM1 业务管理器：未单独落文件的契约实现，供 JniBridge 链接与渐进替换。
// **`DeviceSessionManager` / `CertPinningManager` / `UserStatusManager`**：委托 **`mm2::MM2`** → **`SqliteMetadataDb`**（**`user_version=11`** 表），**进程重启可恢复**（须 **`MM2::Initialize`**）。**在线态**磁盘为**最后已知**缓存，**服务端**仍为权威。
// MessageEditManager / MessageEditOrchestration / MentionPermissionManager / FriendNoteManager / AccountDeleteManager：见独立 `.cpp`。
#include "mm1/managers/CertPinningManager.h"
#include "mm1/managers/DeviceSessionManager.h"
#include "mm1/managers/SystemControl.h"
#include "mm1/managers/UserStatusManager.h"

#include "mm1/MM1.h"
#include "mm2/MM2.h"
#include "Types.h"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace ZChatIM::mm1 {

    // FriendManager / FriendVerificationManager：见 FriendManager.cpp、FriendVerificationManager.cpp

    // GroupManager：见 GroupManager.cpp

    // -------------------------------------------------------------------------
    // SystemControl（委托 MM1 单例；与 JniBridge::EmergencyWipe 同源可信区语义）
    // -------------------------------------------------------------------------
    void SystemControl::EmergencyWipe()
    {
        MM1::Instance().EmergencyTrustedZoneWipe();
    }

    std::map<std::string, std::string> SystemControl::GetStatus()
    {
        return MM1::Instance().SystemControlStatusSnapshot();
    }

    bool SystemControl::RotateKeys()
    {
        const std::vector<uint8_t> k = MM1::Instance().RefreshMasterKey();
        return !k.empty();
    }

    // -------------------------------------------------------------------------
    // DeviceSessionManager（≤2 设备 / 用户；**SQLite 持久化**）
    // -------------------------------------------------------------------------
    namespace {

        constexpr uint64_t kDeviceSessionIdleTimeoutMs = 30ULL * 60ULL * 1000ULL;

        uint64_t NowUnixEpochMsDev()
        {
            using namespace std::chrono;
            return static_cast<uint64_t>(
                duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
        }

        uint64_t ClampFutureNowMsForDeviceCleanup(uint64_t nowMs)
        {
            const uint64_t srv = NowUnixEpochMsDev();
            constexpr uint64_t kMaxFutureSkewMs = 120000;
            if (nowMs > srv && (nowMs - srv) > kMaxFutureSkewMs) {
                return srv;
            }
            return nowMs;
        }

    } // namespace

    bool DeviceSessionManager::RegisterDeviceSession(
        const std::vector<uint8_t>& userId,
        const std::vector<uint8_t>& deviceId,
        const std::vector<uint8_t>& sessionId,
        uint64_t                    loginTimeMs,
        uint64_t                    lastActiveMs,
        std::vector<uint8_t>&       outKickedSessionId)
    {
        outKickedSessionId.clear();
        if (userId.size() != USER_ID_SIZE || deviceId.size() != USER_ID_SIZE || sessionId.size() != JNI_AUTH_SESSION_TOKEN_BYTES) {
            return false;
        }
        return mm2::MM2::Instance().Mm1RegisterDeviceSession(
            userId,
            deviceId,
            sessionId,
            loginTimeMs,
            lastActiveMs,
            outKickedSessionId);
    }

    bool DeviceSessionManager::UpdateLastActive(
        const std::vector<uint8_t>& userId,
        const std::vector<uint8_t>& sessionId,
        uint64_t                    nowMs)
    {
        if (userId.size() != USER_ID_SIZE || sessionId.size() != JNI_AUTH_SESSION_TOKEN_BYTES) {
            return false;
        }
        return mm2::MM2::Instance().Mm1UpdateDeviceSessionLastActive(userId, sessionId, nowMs);
    }

    std::vector<DeviceSessionEntry> DeviceSessionManager::GetDeviceSessions(const std::vector<uint8_t>& userId) const
    {
        if (userId.size() != USER_ID_SIZE) {
            return {};
        }
        std::vector<std::vector<uint8_t>> sids;
        std::vector<std::vector<uint8_t>> dids;
        std::vector<uint64_t>             logins;
        std::vector<uint64_t>             lasts;
        if (!mm2::MM2::Instance().Mm1ListDeviceSessions(userId, sids, dids, logins, lasts)) {
            return {};
        }
        std::vector<DeviceSessionEntry> out;
        out.reserve(sids.size());
        for (size_t i = 0; i < sids.size(); ++i) {
            DeviceSessionEntry d;
            d.sessionId    = std::move(sids[i]);
            d.deviceId     = std::move(dids[i]);
            d.loginTimeMs  = logins[i];
            d.lastActiveMs = lasts[i];
            out.push_back(std::move(d));
        }
        return out;
    }

    void DeviceSessionManager::CleanupExpiredSessions(uint64_t nowMs)
    {
        const uint64_t t = ClampFutureNowMsForDeviceCleanup(nowMs);
        (void)mm2::MM2::Instance().Mm1CleanupExpiredDeviceSessions(t, kDeviceSessionIdleTimeoutMs);
    }

    void DeviceSessionManager::ClearAllRegistrations()
    {
        (void)mm2::MM2::Instance().Mm1ClearAllDeviceSessions();
    }

    // -------------------------------------------------------------------------
    // UserStatusManager（**`mm1_user_status`**；**MM2 未初始化** 时回退进程内 map）
    // -------------------------------------------------------------------------
    namespace {
        std::mutex                  g_st_mtx;
        std::map<std::string, bool> g_st_online;

        std::string UserKey16(const std::vector<uint8_t>& u)
        {
            return std::string(reinterpret_cast<const char*>(u.data()), u.size());
        }
    } // namespace

    void UserStatusManager::SetUserOnline(const std::vector<uint8_t>& userId, bool online)
    {
        if (userId.size() != USER_ID_SIZE) {
            return;
        }
        mm2::MM2& m2 = mm2::MM2::Instance();
        if (m2.IsInitialized()) {
            using namespace std::chrono;
            const uint64_t ms = static_cast<uint64_t>(
                duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
            (void)m2.Mm1UpsertUserStatus(userId, online, ms);
            return;
        }
        std::lock_guard<std::mutex> lk(g_st_mtx);
        g_st_online[UserKey16(userId)] = online;
    }

    bool UserStatusManager::GetUserStatusOnline(const std::vector<uint8_t>& userId) const
    {
        return GetUserStatus(userId);
    }

    bool UserStatusManager::GetUserStatus(const std::vector<uint8_t>& userId) const
    {
        if (userId.size() != USER_ID_SIZE) {
            return false;
        }
        mm2::MM2& m2 = mm2::MM2::Instance();
        if (m2.IsInitialized()) {
            bool on    = false;
            bool found = false;
            if (!m2.Mm1GetUserStatus(userId, on, found)) {
                return false;
            }
            return found && on;
        }
        std::lock_guard<std::mutex> lk(g_st_mtx);
        const auto                    it = g_st_online.find(UserKey16(userId));
        return it != g_st_online.end() && it->second;
    }

    void UserStatusManager::ClearAll()
    {
        std::lock_guard<std::mutex> lk(g_st_mtx);
        g_st_online.clear();
        (void)mm2::MM2::Instance().Mm1ClearAllUserStatus();
    }

    // -------------------------------------------------------------------------
    // CertPinningManager（**SQLite**；失败计数 / 封禁见 **`MM2::Mm1CertPinningRecordFailure`**）
    // -------------------------------------------------------------------------

    void CertPinningManager::ConfigurePinnedPublicKeyHashes(
        const std::vector<uint8_t>& currentSpkiSha256,
        const std::vector<uint8_t>& standbySpkiSha256)
    {
        (void)mm2::MM2::Instance().Mm1CertPinningConfigure(currentSpkiSha256, standbySpkiSha256);
    }

    bool CertPinningManager::VerifyPinnedServerCertificate(
        const std::vector<uint8_t>& /*clientId*/,
        const std::vector<uint8_t>& presentedSpkiSha256)
    {
        return mm2::MM2::Instance().Mm1CertPinningVerify(presentedSpkiSha256);
    }

    bool CertPinningManager::IsClientBanned(const std::vector<uint8_t>& clientId) const
    {
        return mm2::MM2::Instance().Mm1CertPinningIsBanned(clientId);
    }

    void CertPinningManager::RecordFailure(const std::vector<uint8_t>& clientId)
    {
        (void)mm2::MM2::Instance().Mm1CertPinningRecordFailure(clientId);
    }

    void CertPinningManager::ClearBan(const std::vector<uint8_t>& clientId)
    {
        (void)mm2::MM2::Instance().Mm1CertPinningClearBan(clientId);
    }

    void CertPinningManager::ResetPinningState()
    {
        (void)mm2::MM2::Instance().Mm1CertPinningResetAll();
    }

} // namespace ZChatIM::mm1
