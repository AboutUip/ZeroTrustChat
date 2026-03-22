// MM1 业务管理器：未单独落文件的契约实现（进程内内存桩 / 薄封装），供 JniBridge 链接与渐进替换。
#include "mm1/managers/AccountDeleteManager.h"
#include "mm1/managers/CertPinningManager.h"
#include "mm1/managers/DeviceSessionManager.h"
#include "mm1/managers/FriendNoteManager.h"
#include "mm1/managers/MentionPermissionManager.h"
#include "mm1/managers/MessageEditManager.h"
#include "mm1/managers/MessageEditOrchestration.h"
#include "mm1/managers/SystemControl.h"
#include "mm1/managers/UserStatusManager.h"

#include "common/Memory.h"
#include "mm2/MM2.h"
#include "Types.h"

#include <algorithm>
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
    // SystemControl（桩；高危路径不默认清库）
    // -------------------------------------------------------------------------
    void SystemControl::EmergencyWipe()
    {
    }

    std::map<std::string, std::string> SystemControl::GetStatus()
    {
        return {};
    }

    bool SystemControl::RotateKeys()
    {
        return false;
    }

    // -------------------------------------------------------------------------
    // DeviceSessionManager（进程内 ≤2 设备会话）
    // -------------------------------------------------------------------------
    namespace {

        struct DevSess {
            std::vector<uint8_t> sessionId;
            std::vector<uint8_t> deviceId;
            uint64_t             loginTimeMs = 0;
            uint64_t             lastActiveMs = 0;
        };

        std::mutex                                 g_dev_mtx;
        std::map<std::string, std::vector<DevSess>> g_dev_by_user;

        std::string UserKey16(const std::vector<uint8_t>& u)
        {
            return std::string(reinterpret_cast<const char*>(u.data()), u.size());
        }

        bool FindDevSessionLocked(
            const std::vector<uint8_t>& sessionId,
            std::string&                outUserKey,
            size_t&                     outIdx)
        {
            for (auto& pr : g_dev_by_user) {
                for (size_t i = 0; i < pr.second.size(); ++i) {
                    if (pr.second[i].sessionId == sessionId) {
                        outUserKey = pr.first;
                        outIdx     = i;
                        return true;
                    }
                }
            }
            return false;
        }

        void EraseDevSessionAtLocked(const std::string& userKey, size_t idx)
        {
            auto it = g_dev_by_user.find(userKey);
            if (it == g_dev_by_user.end() || idx >= it->second.size()) {
                return;
            }
            it->second.erase(it->second.begin() + static_cast<std::ptrdiff_t>(idx));
            if (it->second.empty()) {
                g_dev_by_user.erase(it);
            }
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

        std::lock_guard<std::mutex> lk(g_dev_mtx);
        const std::string             uk = UserKey16(userId);
        auto&                         vec = g_dev_by_user[uk];

        // 移除同 sessionId 的旧登记（任意用户桶内）
        {
            std::string oldUk;
            size_t      oldIdx = 0;
            if (FindDevSessionLocked(sessionId, oldUk, oldIdx)) {
                EraseDevSessionAtLocked(oldUk, oldIdx);
            }
        }

        // 重新取引用（可能因 Erase 失效）
        auto& vec2 = g_dev_by_user[uk];

        if (vec2.size() >= 2) {
            auto earliest = std::min_element(
                vec2.begin(),
                vec2.end(),
                [](const DevSess& a, const DevSess& b) { return a.loginTimeMs < b.loginTimeMs; });
            if (earliest != vec2.end()) {
                outKickedSessionId = earliest->sessionId;
                vec2.erase(earliest);
            }
        }

        DevSess e;
        e.sessionId    = sessionId;
        e.deviceId     = deviceId;
        e.loginTimeMs  = loginTimeMs;
        e.lastActiveMs = lastActiveMs;
        g_dev_by_user[uk].push_back(std::move(e));
        return true;
    }

    bool DeviceSessionManager::UpdateLastActive(
        const std::vector<uint8_t>& userId,
        const std::vector<uint8_t>& sessionId,
        uint64_t                    nowMs)
    {
        if (userId.size() != USER_ID_SIZE || sessionId.size() != JNI_AUTH_SESSION_TOKEN_BYTES) {
            return false;
        }
        std::lock_guard<std::mutex> lk(g_dev_mtx);
        std::string                   ukFound;
        size_t                        idx = 0;
        if (!FindDevSessionLocked(sessionId, ukFound, idx)) {
            return false;
        }
        if (ukFound != UserKey16(userId)) {
            return false;
        }
        auto& vec = g_dev_by_user[ukFound];
        if (idx >= vec.size()) {
            return false;
        }
        vec[idx].lastActiveMs = nowMs;
        return true;
    }

    std::vector<DeviceSessionEntry> DeviceSessionManager::GetDeviceSessions(const std::vector<uint8_t>& userId) const
    {
        if (userId.size() != USER_ID_SIZE) {
            return {};
        }
        std::lock_guard<std::mutex> lk(g_dev_mtx);
        const std::string             uk = UserKey16(userId);
        const auto                    it = g_dev_by_user.find(uk);
        if (it == g_dev_by_user.end()) {
            return {};
        }
        std::vector<DeviceSessionEntry> out;
        out.reserve(it->second.size());
        for (const auto& e : it->second) {
            DeviceSessionEntry d;
            d.sessionId     = e.sessionId;
            d.deviceId      = e.deviceId;
            d.loginTimeMs   = e.loginTimeMs;
            d.lastActiveMs  = e.lastActiveMs;
            out.push_back(std::move(d));
        }
        return out;
    }

    void DeviceSessionManager::CleanupExpiredSessions(uint64_t /*nowMs*/)
    {
    }

    // -------------------------------------------------------------------------
    // UserStatusManager（进程内）
    // -------------------------------------------------------------------------
    namespace {
        std::mutex                        g_st_mtx;
        std::map<std::string, bool>       g_st_online;
    } // namespace

    void UserStatusManager::SetUserOnline(const std::vector<uint8_t>& userId, bool online)
    {
        if (userId.size() != USER_ID_SIZE) {
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
        std::lock_guard<std::mutex> lk(g_st_mtx);
        const auto                    it = g_st_online.find(UserKey16(userId));
        return it != g_st_online.end() && it->second;
    }

    // -------------------------------------------------------------------------
    // MessageEditManager（ApplyEdit 桩；GetEditState 委托 MM2）
    // -------------------------------------------------------------------------
    bool MessageEditManager::CheckEditAllowed(
        const std::vector<uint8_t>& /*messageId*/,
        const std::vector<uint8_t>& /*senderId*/,
        uint64_t /*editTimestampSeconds*/,
        const std::vector<uint8_t>& /*signature*/,
        uint32_t /*currentEditCount*/) const
    {
        return false;
    }

    bool MessageEditManager::GetEditState(
        const std::vector<uint8_t>& messageId,
        uint32_t&                   outEditCount,
        uint64_t&                   outLastEditTimeSeconds) const
    {
        outEditCount            = 0;
        outLastEditTimeSeconds  = 0;
        return mm2::MM2::Instance().GetMessageEditState(messageId, outEditCount, outLastEditTimeSeconds);
    }

    bool MessageEditManager::ApplyEdit(
        const std::vector<uint8_t>& /*messageId*/,
        const std::vector<uint8_t>& /*newEncryptedContent*/,
        uint64_t /*editTimestampSeconds*/,
        const std::vector<uint8_t>& /*senderId*/,
        const std::vector<uint8_t>& /*signature*/)
    {
        return false;
    }

    // -------------------------------------------------------------------------
    // AccountDeleteManager（桩）
    // -------------------------------------------------------------------------
    bool AccountDeleteManager::DeleteAccount(
        const std::vector<uint8_t>& /*userId*/,
        const std::vector<uint8_t>& /*reauthToken*/,
        const std::vector<uint8_t>& /*secondConfirmToken*/)
    {
        return false;
    }

    bool AccountDeleteManager::IsAccountDeleted(const std::vector<uint8_t>& /*userId*/) const
    {
        return false;
    }

    // -------------------------------------------------------------------------
    // CertPinningManager（最小内存实现）
    // -------------------------------------------------------------------------
    namespace {
        std::mutex                  g_cert_mtx;
        std::vector<uint8_t>        g_spki_cur;
        std::vector<uint8_t>        g_spki_stdby;
        std::map<std::vector<uint8_t>, unsigned> g_fail;
        std::map<std::vector<uint8_t>, bool>       g_banned;
    } // namespace

    void CertPinningManager::ConfigurePinnedPublicKeyHashes(
        const std::vector<uint8_t>& currentSpkiSha256,
        const std::vector<uint8_t>& standbySpkiSha256)
    {
        std::lock_guard<std::mutex> lk(g_cert_mtx);
        g_spki_cur   = currentSpkiSha256;
        g_spki_stdby = standbySpkiSha256;
    }

    bool CertPinningManager::VerifyPinnedServerCertificate(
        const std::vector<uint8_t>& /*clientId*/,
        const std::vector<uint8_t>& presentedSpkiSha256)
    {
        std::lock_guard<std::mutex> lk(g_cert_mtx);
        if (presentedSpkiSha256.size() != SHA256_SIZE) {
            return false;
        }
        const bool okCur =
            g_spki_cur.size() == SHA256_SIZE
            && common::Memory::ConstantTimeCompare(g_spki_cur.data(), presentedSpkiSha256.data(), SHA256_SIZE);
        const bool okSt =
            g_spki_stdby.size() == SHA256_SIZE
            && common::Memory::ConstantTimeCompare(g_spki_stdby.data(), presentedSpkiSha256.data(), SHA256_SIZE);
        return okCur || okSt;
    }

    bool CertPinningManager::IsClientBanned(const std::vector<uint8_t>& clientId) const
    {
        std::lock_guard<std::mutex> lk(g_cert_mtx);
        const auto                    it = g_banned.find(clientId);
        return it != g_banned.end() && it->second;
    }

    void CertPinningManager::RecordFailure(const std::vector<uint8_t>& clientId)
    {
        std::lock_guard<std::mutex> lk(g_cert_mtx);
        ++g_fail[clientId];
        if (g_fail[clientId] >= 10) {
            g_banned[clientId] = true;
        }
    }

    void CertPinningManager::ClearBan(const std::vector<uint8_t>& clientId)
    {
        std::lock_guard<std::mutex> lk(g_cert_mtx);
        g_fail.erase(clientId);
        g_banned.erase(clientId);
    }

    // -------------------------------------------------------------------------
    // MessageEditOrchestration（桩）
    // -------------------------------------------------------------------------
    bool MessageEditOrchestration::EditMessage(
        const std::vector<uint8_t>& /*messageId*/,
        const std::vector<uint8_t>& /*newEncryptedContent*/,
        uint64_t /*editTimestampSeconds*/,
        const std::vector<uint8_t>& /*signature*/,
        const std::vector<uint8_t>& /*senderId*/)
    {
        return false;
    }

    // -------------------------------------------------------------------------
    // FriendNoteManager（桩）
    // -------------------------------------------------------------------------
    bool FriendNoteManager::UpdateFriendNote(
        const std::vector<uint8_t>& /*userId*/,
        const std::vector<uint8_t>& /*friendId*/,
        const std::vector<uint8_t>& /*newEncryptedNote*/,
        uint64_t /*updateTimestampSeconds*/,
        const std::vector<uint8_t>& /*signatureEd25519*/)
    {
        return false;
    }

    // -------------------------------------------------------------------------
    // MentionPermissionManager（桩）
    // -------------------------------------------------------------------------
    bool MentionPermissionManager::ValidateMentionRequest(
        const std::vector<uint8_t>& /*groupId*/,
        const std::vector<uint8_t>& /*senderId*/,
        int32_t /*mentionType*/,
        const std::vector<std::vector<uint8_t>>& /*mentionedUserIds*/,
        uint64_t /*nowMs*/,
        const std::vector<uint8_t>& /*signatureEd25519*/)
    {
        return false;
    }

    bool MentionPermissionManager::RecordMentionAtAllUsage(
        const std::vector<uint8_t>& /*groupId*/,
        const std::vector<uint8_t>& /*senderId*/,
        uint64_t /*nowMs*/)
    {
        return false;
    }

} // namespace ZChatIM::mm1
