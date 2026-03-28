#include "jni/JniBridge.h"

#include "common/Memory.h"
#include "mm1/managers/FriendManager.h"
#include "mm1/MM1.h"
#include "mm1/managers/RtcCallSessionManager.h"
#include "mm2/MM2.h"
#include "Types.h"

#include <cstdint>
#include <exception>
#include <filesystem>
#include <limits>
#include <sstream>
#include <utility>

namespace ZChatIM::jni {

    namespace {

        bool PrincipalMatches(const std::vector<uint8_t>& principal, const std::vector<uint8_t>& userId)
        {
            if (principal.size() != USER_ID_SIZE || userId.size() != USER_ID_SIZE) {
                return false;
            }
            return common::Memory::ConstantTimeCompare(principal.data(), userId.data(), USER_ID_SIZE);
        }

        bool IsSelfOrAcceptedFriend(
            mm1::MM1&                  mm1,
            const std::vector<uint8_t>& principal,
            const std::vector<uint8_t>& peer)
        {
            if (principal.size() != USER_ID_SIZE || peer.size() != USER_ID_SIZE) {
                return false;
            }
            if (PrincipalMatches(principal, peer)) {
                return true;
            }
            const auto friends = mm1.GetFriendManager().GetFriends(principal);
            for (const auto& f : friends) {
                if (f.size() == USER_ID_SIZE
                    && common::Memory::ConstantTimeCompare(f.data(), peer.data(), USER_ID_SIZE)) {
                    return true;
                }
            }
            return false;
        }

        bool TryBindCaller(mm1::MM1& mm1, const std::vector<uint8_t>& callerSessionId, std::vector<uint8_t>& outPrincipal)
        {
            outPrincipal.clear();
            if (callerSessionId.size() != JNI_AUTH_SESSION_TOKEN_BYTES) {
                return false;
            }
            return mm1.GetAuthSessionManager().TryGetSessionUserId(callerSessionId, outPrincipal)
                && outPrincipal.size() == USER_ID_SIZE;
        }

        std::string NormalizeLocalPathUtf8ForCompare(const std::string& utf8)
        {
            if (utf8.empty()) {
                return {};
            }
            return std::filesystem::path(utf8).lexically_normal().generic_string();
        }

        std::vector<std::vector<uint8_t>> PackSessionMessageRows(
            const std::vector<std::pair<std::vector<uint8_t>, std::vector<uint8_t>>>& rows)
        {
            std::vector<std::vector<uint8_t>> out;
            out.reserve(rows.size());
            for (const auto& pr : rows) {
                const auto& id      = pr.first;
                const auto& payload = pr.second;
                if (id.size() != MESSAGE_ID_SIZE) {
                    continue;
                }
                if (payload.size() > static_cast<size_t>(std::numeric_limits<uint32_t>::max())) {
                    continue;
                }
                std::vector<uint8_t> row;
                const uint32_t       len = static_cast<uint32_t>(payload.size());
                row.reserve(MESSAGE_ID_SIZE + 4 + payload.size());
                row.insert(row.end(), id.begin(), id.end());
                row.push_back(static_cast<uint8_t>((len >> 24) & 0xFF));
                row.push_back(static_cast<uint8_t>((len >> 16) & 0xFF));
                row.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
                row.push_back(static_cast<uint8_t>(len & 0xFF));
                row.insert(row.end(), payload.begin(), payload.end());
                out.push_back(std::move(row));
            }
            return out;
        }

        void AppendBe64(std::vector<uint8_t>& row, uint64_t v)
        {
            for (int s = 56; s >= 0; s -= 8) {
                row.push_back(static_cast<uint8_t>((v >> s) & 0xFF));
            }
        }

        std::vector<std::vector<uint8_t>> PackDeviceSessions(const std::vector<mm1::DeviceSessionEntry>& entries)
        {
            std::vector<std::vector<uint8_t>> out;
            out.reserve(entries.size());
            for (const auto& e : entries) {
                std::vector<uint8_t> row;
                row.reserve(48);
                if (e.sessionId.size() != JNI_AUTH_SESSION_TOKEN_BYTES || e.deviceId.size() != USER_ID_SIZE) {
                    continue;
                }
                row.insert(row.end(), e.sessionId.begin(), e.sessionId.end());
                row.insert(row.end(), e.deviceId.begin(), e.deviceId.end());
                AppendBe64(row, e.loginTimeMs);
                AppendBe64(row, e.lastActiveMs);
                if (row.size() == 48) {
                    out.push_back(std::move(row));
                }
            }
            return out;
        }

    } // namespace

    JniBridge& JniBridge::Instance()
    {
        static JniBridge s_instance;
        return s_instance;
    }

    JniBridge::JniBridge()
        : m_mm1(mm1::MM1::Instance())
        , m_mm2(mm2::MM2::Instance())
    {
    }

    JniBridge::~JniBridge() = default;

    bool JniBridge::CheckInitialized()
    {
        return m_initialized.load(std::memory_order_acquire);
    }

    void JniBridge::LogOperation(const std::string& /*operation*/, bool /*success*/)
    {
    }

    bool JniBridge::Initialize(const std::string& dataDir, const std::string& indexDir)
    {
        return Initialize(dataDir, indexDir, nullptr);
    }

    bool JniBridge::Initialize(
        const std::string& dataDir,
        const std::string& indexDir,
        const char*        messageKeyPassphraseUtf8)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        try {
            m_lastInitializeError.clear();
            if (dataDir.empty() || indexDir.empty()) {
                m_lastInitializeError = "empty dataDir or indexDir";
                return false;
            }
            if (m_initialized.load(std::memory_order_acquire)) {
                if (!m_mm2.IsInitialized()) {
                    m_initialized.store(false, std::memory_order_release);
                } else {
                    const std::string wantD = NormalizeLocalPathUtf8ForCompare(dataDir);
                    const std::string wantI = NormalizeLocalPathUtf8ForCompare(indexDir);
                    if (wantD != NormalizeLocalPathUtf8ForCompare(m_mm2.GetDataDirUtf8())
                        || wantI != NormalizeLocalPathUtf8ForCompare(m_mm2.GetIndexDirUtf8())) {
                        m_lastInitializeError =
                            "already initialized with different data-dir or index-dir (re-init not allowed)";
                        return false;
                    }
                    return true;
                }
            }
            if (!m_mm1.Initialize()) {
                m_lastInitializeError = "MM1::Initialize failed (Crypto::Init)";
                return false;
            }
            if (!m_mm2.Initialize(dataDir, indexDir, messageKeyPassphraseUtf8)) {
                std::string e = m_mm2.LastError();
                if (e.empty()) {
                    e = "MM2::Initialize failed";
                }
                m_lastInitializeError = std::move(e);
                m_mm1.Cleanup();
                return false;
            }
            m_initialized.store(true, std::memory_order_release);
            m_lastInitializeError.clear();
            return true;
        } catch (const std::exception& ex) {
            m_lastInitializeError = std::string("Initialize exception: ") + ex.what();
            m_initialized.store(false, std::memory_order_release);
            try {
                m_mm2.Cleanup();
                m_mm1.Cleanup();
            } catch (...) {
            }
            return false;
        } catch (...) {
            m_lastInitializeError = "Initialize exception: unknown";
            m_initialized.store(false, std::memory_order_release);
            try {
                m_mm2.Cleanup();
                m_mm1.Cleanup();
            } catch (...) {
            }
            return false;
        }
    }

    std::string JniBridge::LastInitializeError() const
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        return m_lastInitializeError;
    }

    void JniBridge::Cleanup()
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        try {
            m_mm2.Cleanup();
            m_mm1.ClearAllAuthSessions();
            m_mm1.Cleanup();
            NotifyExternalTrustedZoneWipeHandled();
        } catch (...) {
            // JNI 边界必须避免异常穿透至 JVM。
            m_initialized.store(false, std::memory_order_release);
        }
    }

    void JniBridge::NotifyExternalTrustedZoneWipeHandled()
    {
        m_initialized.store(false, std::memory_order_release);
    }

    std::vector<uint8_t> JniBridge::Auth(
        const std::vector<uint8_t>& userId,
        const std::vector<uint8_t>& token,
        const std::vector<uint8_t>& clientIp)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return {};
        }
        return m_mm1.GetAuthSessionManager().Auth(userId, token, clientIp);
    }

    bool JniBridge::VerifySession(const std::vector<uint8_t>& sessionId)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return false;
        }
        return m_mm1.GetAuthSessionManager().VerifySession(sessionId);
    }

    bool JniBridge::DestroySession(
        const std::vector<uint8_t>& callerSessionId,
        const std::vector<uint8_t>& sessionIdToDestroy)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return false;
        }
        std::vector<uint8_t> pCaller;
        std::vector<uint8_t> pTarget;
        if (!TryBindCaller(m_mm1, callerSessionId, pCaller)) {
            return false;
        }
        if (!TryBindCaller(m_mm1, sessionIdToDestroy, pTarget)) {
            return false;
        }
        if (!PrincipalMatches(pCaller, pTarget)) {
            return false;
        }
        return m_mm1.GetAuthSessionManager().DestroySession(sessionIdToDestroy);
    }

    bool JniBridge::RegisterLocalUser(
        const std::vector<uint8_t>& userId,
        const std::vector<uint8_t>& passwordUtf8,
        const std::vector<uint8_t>& recoverySecretUtf8)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return false;
        }
        return m_mm1.GetLocalAccountCredentialManager().RegisterLocalUser(userId, passwordUtf8, recoverySecretUtf8);
    }

    std::vector<uint8_t> JniBridge::AuthWithLocalPassword(
        const std::vector<uint8_t>& userId,
        const std::vector<uint8_t>& passwordUtf8,
        const std::vector<uint8_t>& clientIp)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return {};
        }
        mm1::AuthSessionManager& auth = m_mm1.GetAuthSessionManager();
        return m_mm1.GetLocalAccountCredentialManager().AuthWithLocalPassword(auth, userId, passwordUtf8, clientIp);
    }

    bool JniBridge::HasLocalPassword(const std::vector<uint8_t>& userId)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return false;
        }
        return m_mm1.GetLocalAccountCredentialManager().HasLocalPassword(userId);
    }

    bool JniBridge::ChangeLocalPassword(
        const std::vector<uint8_t>& callerSessionId,
        const std::vector<uint8_t>& userId,
        const std::vector<uint8_t>& oldPasswordUtf8,
        const std::vector<uint8_t>& newPasswordUtf8)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return false;
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return false;
        }
        if (!PrincipalMatches(principal, userId)) {
            return false;
        }
        return m_mm1.GetLocalAccountCredentialManager().ChangeLocalPassword(userId, oldPasswordUtf8, newPasswordUtf8);
    }

    bool JniBridge::ResetLocalPasswordWithRecovery(
        const std::vector<uint8_t>& userId,
        const std::vector<uint8_t>& recoverySecretUtf8,
        const std::vector<uint8_t>& newPasswordUtf8,
        const std::vector<uint8_t>& clientIp)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return false;
        }
        mm1::AuthSessionManager& auth = m_mm1.GetAuthSessionManager();
        return m_mm1.GetLocalAccountCredentialManager().ResetLocalPasswordWithRecovery(
            auth, userId, recoverySecretUtf8, newPasswordUtf8, clientIp);
    }

    std::vector<uint8_t> JniBridge::RtcStartCall(
        const std::vector<uint8_t>& callerSessionId,
        const std::vector<uint8_t>& peerUserId,
        int32_t                     callKind)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return {};
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return {};
        }
        if (peerUserId.size() != USER_ID_SIZE) {
            return {};
        }
        if (callKind != mm1::RTC_CALL_KIND_AUDIO && callKind != mm1::RTC_CALL_KIND_VIDEO) {
            return {};
        }
        return m_mm1.GetRtcCallSessionManager().StartCall(principal, peerUserId, callKind);
    }

    bool JniBridge::RtcAcceptCall(
        const std::vector<uint8_t>& callerSessionId,
        const std::vector<uint8_t>& callId)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return false;
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return false;
        }
        if (callId.size() != MESSAGE_ID_SIZE) {
            return false;
        }
        return m_mm1.GetRtcCallSessionManager().AcceptCall(principal, callId);
    }

    bool JniBridge::RtcRejectCall(
        const std::vector<uint8_t>& callerSessionId,
        const std::vector<uint8_t>& callId)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return false;
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return false;
        }
        if (callId.size() != MESSAGE_ID_SIZE) {
            return false;
        }
        return m_mm1.GetRtcCallSessionManager().RejectCall(principal, callId);
    }

    bool JniBridge::RtcEndCall(
        const std::vector<uint8_t>& callerSessionId,
        const std::vector<uint8_t>& callId)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return false;
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return false;
        }
        if (callId.size() != MESSAGE_ID_SIZE) {
            return false;
        }
        return m_mm1.GetRtcCallSessionManager().EndCall(principal, callId);
    }

    int32_t JniBridge::RtcGetCallState(
        const std::vector<uint8_t>& callerSessionId,
        const std::vector<uint8_t>& callId)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return mm1::RTC_CALL_STATE_INVALID;
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return mm1::RTC_CALL_STATE_INVALID;
        }
        if (callId.size() != MESSAGE_ID_SIZE) {
            return mm1::RTC_CALL_STATE_INVALID;
        }
        return m_mm1.GetRtcCallSessionManager().GetCallState(principal, callId);
    }

    int32_t JniBridge::RtcGetCallKind(
        const std::vector<uint8_t>& callerSessionId,
        const std::vector<uint8_t>& callId)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return -1;
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return -1;
        }
        if (callId.size() != MESSAGE_ID_SIZE) {
            return -1;
        }
        return m_mm1.GetRtcCallSessionManager().GetCallKind(principal, callId);
    }

    std::vector<uint8_t> JniBridge::StoreMessage(
        const std::vector<uint8_t>& callerSessionId,
        const std::vector<uint8_t>& imSessionId,
        const std::vector<uint8_t>& payload)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return {};
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return {};
        }
        if (imSessionId.size() != USER_ID_SIZE) {
            return {};
        }
        std::vector<uint8_t> mid;
        if (!m_mm2.StoreMessage(imSessionId, principal, payload, mid)) {
            return {};
        }
        return mid;
    }

    std::vector<uint8_t> JniBridge::RetrieveMessage(
        const std::vector<uint8_t>& callerSessionId,
        const std::vector<uint8_t>& messageId)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return {};
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return {};
        }
        (void)principal;
        std::vector<uint8_t> out;
        if (!m_mm2.RetrieveMessage(messageId, out)) {
            return {};
        }
        return out;
    }

    bool JniBridge::DeleteMessage(
        const std::vector<uint8_t>& callerSessionId,
        const std::vector<uint8_t>& messageId,
        const std::vector<uint8_t>& senderId,
        const std::vector<uint8_t>& signatureEd25519)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return false;
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return false;
        }
        if (!PrincipalMatches(principal, senderId)) {
            return false;
        }
        return m_mm1.GetMessageRecallManager().DeleteMessage(messageId, senderId, signatureEd25519);
    }

    bool JniBridge::RecallMessage(
        const std::vector<uint8_t>& callerSessionId,
        const std::vector<uint8_t>& messageId,
        const std::vector<uint8_t>& senderId,
        const std::vector<uint8_t>& signatureEd25519)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return false;
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return false;
        }
        if (!PrincipalMatches(principal, senderId)) {
            return false;
        }
        return m_mm1.GetMessageRecallManager().RecallMessage(messageId, senderId, signatureEd25519);
    }

    std::vector<std::vector<uint8_t>> JniBridge::ListMessages(
        const std::vector<uint8_t>& callerSessionId,
        const std::vector<uint8_t>& userId,
        int                         count)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return {};
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return {};
        }
        if (!PrincipalMatches(principal, userId)) {
            return {};
        }
        return m_mm2.GetMessageQueryManager().ListMessages(userId, count);
    }

    std::vector<std::vector<uint8_t>> JniBridge::ListMessagesSinceTimestamp(
        const std::vector<uint8_t>& callerSessionId,
        const std::vector<uint8_t>& userId,
        uint64_t                    sinceTimestampMs,
        int                         count)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return {};
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return {};
        }
        if (!PrincipalMatches(principal, userId)) {
            return {};
        }
        return m_mm2.GetMessageQueryManager().ListMessagesSinceTimestamp(userId, sinceTimestampMs, count);
    }

    std::vector<std::vector<uint8_t>> JniBridge::ListMessagesSinceMessageId(
        const std::vector<uint8_t>& callerSessionId,
        const std::vector<uint8_t>& userId,
        const std::vector<uint8_t>& lastMsgId,
        int                         count)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return {};
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return {};
        }
        (void)principal;
        // ZSP SYNC / 网关：第二参数为 **IM sessionId**（16B），与 MM2::ListMessagesSinceMessageId 一致；
        // 不可与 PrincipalMatches(principal, …)（误把其当作 userId）混用，否则增量同步永远返回空。
        return m_mm2.GetMessageQueryManager().ListMessagesSinceMessageId(userId, lastMsgId, count);
    }

    bool JniBridge::MarkMessageRead(
        const std::vector<uint8_t>& callerSessionId,
        const std::vector<uint8_t>& messageId,
        uint64_t                    readTimestampMs)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return false;
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return false;
        }
        (void)principal;
        return m_mm2.MarkMessageRead(messageId, readTimestampMs);
    }

    std::vector<std::vector<uint8_t>> JniBridge::GetUnreadSessionMessageIds(
        const std::vector<uint8_t>& callerSessionId,
        const std::vector<uint8_t>& imSessionId,
        int                         limit)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return {};
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return {};
        }
        (void)principal;
        if (imSessionId.size() != USER_ID_SIZE) {
            return {};
        }
        std::vector<std::pair<std::vector<uint8_t>, uint64_t>> pairs;
        if (!m_mm2.GetUnreadSessionMessages(imSessionId, static_cast<size_t>(limit < 0 ? 0 : limit), pairs)) {
            return {};
        }
        std::vector<std::vector<uint8_t>> out;
        out.reserve(pairs.size());
        for (auto& pr : pairs) {
            out.push_back(std::move(pr.first));
        }
        return out;
    }

    bool JniBridge::StoreMessageReplyRelation(
        const std::vector<uint8_t>& callerSessionId,
        const std::vector<uint8_t>& senderEd25519PublicKey,
        const std::vector<uint8_t>& messageId,
        const std::vector<uint8_t>& repliedMsgId,
        const std::vector<uint8_t>& repliedSenderId,
        const std::vector<uint8_t>& repliedContentDigest,
        const std::vector<uint8_t>& senderId,
        const std::vector<uint8_t>& signatureEd25519)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return false;
        }
        return m_mm1.GetMessageReplyManager().StoreMessageReplyRelation(
            callerSessionId,
            senderEd25519PublicKey,
            messageId,
            repliedMsgId,
            repliedSenderId,
            repliedContentDigest,
            senderId,
            signatureEd25519);
    }

    std::vector<std::vector<uint8_t>> JniBridge::GetMessageReplyRelation(
        const std::vector<uint8_t>& callerSessionId,
        const std::vector<uint8_t>& messageId)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return {};
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return {};
        }
        (void)principal;
        std::vector<uint8_t> a;
        std::vector<uint8_t> b;
        std::vector<uint8_t> c;
        if (!m_mm2.GetMessageReplyRelation(messageId, a, b, c)) {
            return {};
        }
        std::vector<std::vector<uint8_t>> out;
        out.push_back(std::move(a));
        out.push_back(std::move(b));
        out.push_back(std::move(c));
        return out;
    }

    bool JniBridge::EditMessage(
        const std::vector<uint8_t>& callerSessionId,
        const std::vector<uint8_t>& messageId,
        const std::vector<uint8_t>& newEncryptedContent,
        uint64_t                    editTimestampSeconds,
        const std::vector<uint8_t>& signature,
        const std::vector<uint8_t>& senderId)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return false;
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return false;
        }
        if (!PrincipalMatches(principal, senderId)) {
            return false;
        }
        return m_mm1.GetMessageEditManager().ApplyEdit(
            messageId,
            newEncryptedContent,
            editTimestampSeconds,
            senderId,
            signature);
    }

    std::vector<uint8_t> JniBridge::GetMessageEditState(
        const std::vector<uint8_t>& callerSessionId,
        const std::vector<uint8_t>& messageId)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return {};
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return {};
        }
        (void)principal;
        uint32_t ec = 0;
        uint64_t lt = 0;
        if (!m_mm1.GetMessageEditManager().GetEditState(messageId, ec, lt)) {
            return {};
        }
        std::vector<uint8_t> out(12);
        out[0]  = static_cast<uint8_t>((ec >> 24) & 0xFF);
        out[1]  = static_cast<uint8_t>((ec >> 16) & 0xFF);
        out[2]  = static_cast<uint8_t>((ec >> 8) & 0xFF);
        out[3]  = static_cast<uint8_t>(ec & 0xFF);
        out[4]  = static_cast<uint8_t>((lt >> 56) & 0xFF);
        out[5]  = static_cast<uint8_t>((lt >> 48) & 0xFF);
        out[6]  = static_cast<uint8_t>((lt >> 40) & 0xFF);
        out[7]  = static_cast<uint8_t>((lt >> 32) & 0xFF);
        out[8]  = static_cast<uint8_t>((lt >> 24) & 0xFF);
        out[9]  = static_cast<uint8_t>((lt >> 16) & 0xFF);
        out[10] = static_cast<uint8_t>((lt >> 8) & 0xFF);
        out[11] = static_cast<uint8_t>(lt & 0xFF);
        return out;
    }

    bool JniBridge::StoreUserData(
        const std::vector<uint8_t>& callerSessionId,
        const std::vector<uint8_t>& userId,
        int32_t                     type,
        const std::vector<uint8_t>& data)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return false;
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return false;
        }
        if (!PrincipalMatches(principal, userId)) {
            return false;
        }
        if (type == MM1_USER_KV_TYPE_AVATAR_V1) {
            if (data.size() > MM1_USER_AVATAR_MAX_BYTES) {
                return false;
            }
        }
        if (type == MM1_USER_KV_TYPE_DISPLAY_NAME_V1) {
            if (data.size() > MM1_USER_DISPLAY_NAME_MAX_BYTES) {
                return false;
            }
        }
        return m_mm1.GetUserDataManager().StoreUserData(userId, type, data);
    }

    std::vector<uint8_t> JniBridge::GetUserData(
        const std::vector<uint8_t>& callerSessionId,
        const std::vector<uint8_t>& userId,
        int32_t                     type)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return {};
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return {};
        }
        if (type == MM1_USER_KV_TYPE_AVATAR_V1 || type == MM1_USER_KV_TYPE_DISPLAY_NAME_V1) {
            if (!IsSelfOrAcceptedFriend(m_mm1, principal, userId)) {
                return {};
            }
        } else {
            if (!PrincipalMatches(principal, userId)) {
                return {};
            }
        }
        return m_mm1.GetUserDataManager().GetUserData(userId, type);
    }

    bool JniBridge::DeleteUserData(
        const std::vector<uint8_t>& callerSessionId,
        const std::vector<uint8_t>& userId,
        int32_t                     type)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return false;
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return false;
        }
        if (!PrincipalMatches(principal, userId)) {
            return false;
        }
        return m_mm1.GetUserDataManager().DeleteUserData(userId, type);
    }

    std::vector<uint8_t> JniBridge::SendFriendRequest(
        const std::vector<uint8_t>& callerSessionId,
        const std::vector<uint8_t>& fromUserId,
        const std::vector<uint8_t>& toUserId,
        uint64_t                    timestampSeconds,
        const std::vector<uint8_t>& signatureEd25519)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return {};
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return {};
        }
        if (!PrincipalMatches(principal, fromUserId)) {
            return {};
        }
        mm1::SendFriendRequestResult r =
            m_mm1.GetFriendManager().SendFriendRequest(fromUserId, toUserId, timestampSeconds, signatureEd25519);
        if (r.requestId.size() != MESSAGE_ID_SIZE) {
            return {};
        }
        std::vector<uint8_t> out(17);
        std::memcpy(out.data(), r.requestId.data(), MESSAGE_ID_SIZE);
        out[16] = r.duplicatePending ? static_cast<uint8_t>(1) : static_cast<uint8_t>(0);
        return out;
    }

    bool JniBridge::RespondFriendRequest(
        const std::vector<uint8_t>& callerSessionId,
        const std::vector<uint8_t>& requestId,
        bool                        accept,
        const std::vector<uint8_t>& responderId,
        uint64_t                    timestampSeconds,
        const std::vector<uint8_t>& signatureEd25519)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return false;
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return false;
        }
        if (!PrincipalMatches(principal, responderId)) {
            return false;
        }
        return m_mm1.GetFriendManager().RespondFriendRequest(
            requestId,
            accept,
            responderId,
            timestampSeconds,
            signatureEd25519);
    }

    bool JniBridge::DeleteFriend(
        const std::vector<uint8_t>& callerSessionId,
        const std::vector<uint8_t>& userId,
        const std::vector<uint8_t>& friendId,
        uint64_t                    timestampSeconds,
        const std::vector<uint8_t>& signatureEd25519)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return false;
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return false;
        }
        if (!PrincipalMatches(principal, userId)) {
            return false;
        }
        return m_mm1.GetFriendManager().DeleteFriend(userId, friendId, timestampSeconds, signatureEd25519);
    }

    std::vector<std::vector<uint8_t>> JniBridge::GetFriends(
        const std::vector<uint8_t>& callerSessionId,
        const std::vector<uint8_t>& userId)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return {};
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return {};
        }
        if (!PrincipalMatches(principal, userId)) {
            return {};
        }
        return m_mm1.GetFriendManager().GetFriends(userId);
    }

    std::vector<std::vector<uint8_t>> JniBridge::ListPendingFriendRequests(
        const std::vector<uint8_t>& callerSessionId,
        const std::vector<uint8_t>& userId)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return {};
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return {};
        }
        if (!PrincipalMatches(principal, userId)) {
            return {};
        }
        return m_mm1.GetFriendManager().ListPendingIncomingFriendRequests(userId);
    }

    std::vector<uint8_t> JniBridge::CreateGroup(
        const std::vector<uint8_t>& callerSessionId,
        const std::vector<uint8_t>& creatorId,
        const std::string&          name)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return {};
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return {};
        }
        if (!PrincipalMatches(principal, creatorId)) {
            return {};
        }
        return m_mm1.GetGroupManager().CreateGroup(creatorId, name);
    }

    bool JniBridge::InviteMember(
        const std::vector<uint8_t>& callerSessionId,
        const std::vector<uint8_t>& groupId,
        const std::vector<uint8_t>& userId)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return false;
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return false;
        }
        return m_mm1.GetGroupManager().InviteMember(groupId, userId, principal);
    }

    bool JniBridge::RemoveMember(
        const std::vector<uint8_t>& callerSessionId,
        const std::vector<uint8_t>& groupId,
        const std::vector<uint8_t>& userId)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return false;
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return false;
        }
        return m_mm1.GetGroupManager().RemoveMember(groupId, userId, principal);
    }

    bool JniBridge::LeaveGroup(
        const std::vector<uint8_t>& callerSessionId,
        const std::vector<uint8_t>& groupId,
        const std::vector<uint8_t>& userId)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return false;
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return false;
        }
        if (!PrincipalMatches(principal, userId)) {
            return false;
        }
        return m_mm1.GetGroupManager().LeaveGroup(groupId, userId);
    }

    std::vector<std::vector<uint8_t>> JniBridge::GetGroupMembers(
        const std::vector<uint8_t>& callerSessionId,
        const std::vector<uint8_t>& groupId)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return {};
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return {};
        }
        return m_mm1.GetGroupManager().GetGroupMembers(groupId, principal);
    }

    bool JniBridge::UpdateGroupKey(
        const std::vector<uint8_t>& callerSessionId,
        const std::vector<uint8_t>& groupId)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return false;
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return false;
        }
        return m_mm1.GetGroupManager().UpdateGroupKey(groupId, principal);
    }

    bool JniBridge::ValidateMentionRequest(
        const std::vector<uint8_t>&                     callerSessionId,
        const std::vector<uint8_t>&                     groupId,
        const std::vector<uint8_t>&                     senderId,
        int32_t                                         mentionType,
        const std::vector<std::vector<uint8_t>>&        mentionedUserIds,
        uint64_t                                        nowMs,
        const std::vector<uint8_t>&                     signatureEd25519)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return false;
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return false;
        }
        if (!PrincipalMatches(principal, senderId)) {
            return false;
        }
        return m_mm1.GetMentionPermissionManager().ValidateMentionRequest(
            groupId,
            senderId,
            mentionType,
            mentionedUserIds,
            nowMs,
            signatureEd25519);
    }

    bool JniBridge::RecordMentionAtAllUsage(
        const std::vector<uint8_t>& callerSessionId,
        const std::vector<uint8_t>& groupId,
        const std::vector<uint8_t>& senderId,
        uint64_t                    nowMs)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return false;
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return false;
        }
        if (!PrincipalMatches(principal, senderId)) {
            return false;
        }
        return m_mm1.GetMentionPermissionManager().RecordMentionAtAllUsage(groupId, senderId, nowMs);
    }

    bool JniBridge::MuteMember(
        const std::vector<uint8_t>& callerSessionId,
        const std::vector<uint8_t>& groupId,
        const std::vector<uint8_t>& userId,
        const std::vector<uint8_t>& mutedBy,
        uint64_t                    startTimeMs,
        int64_t                     durationSeconds,
        const std::vector<uint8_t>& reason)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return false;
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return false;
        }
        if (!PrincipalMatches(principal, mutedBy)) {
            return false;
        }
        return m_mm1.GetGroupMuteManager().MuteMember(
            groupId,
            userId,
            mutedBy,
            startTimeMs,
            durationSeconds,
            reason);
    }

    bool JniBridge::IsMuted(
        const std::vector<uint8_t>& callerSessionId,
        const std::vector<uint8_t>& groupId,
        const std::vector<uint8_t>& userId,
        uint64_t                    nowMs)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return false;
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return false;
        }
        (void)principal;
        int64_t rem = 0;
        return m_mm1.GetGroupMuteManager().IsMuted(groupId, userId, nowMs, rem);
    }

    bool JniBridge::UnmuteMember(
        const std::vector<uint8_t>& callerSessionId,
        const std::vector<uint8_t>& groupId,
        const std::vector<uint8_t>& userId,
        const std::vector<uint8_t>& unmutedBy)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return false;
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return false;
        }
        if (!PrincipalMatches(principal, unmutedBy)) {
            return false;
        }
        return m_mm1.GetGroupMuteManager().UnmuteMember(groupId, userId, unmutedBy);
    }

    bool JniBridge::UpdateGroupName(
        const std::vector<uint8_t>& callerSessionId,
        const std::vector<uint8_t>& groupId,
        const std::vector<uint8_t>& updaterId,
        const std::string&          newGroupName,
        uint64_t                    nowMs)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return false;
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return false;
        }
        if (!PrincipalMatches(principal, updaterId)) {
            return false;
        }
        return m_mm1.GetGroupNameManager().UpdateGroupName(groupId, updaterId, newGroupName, nowMs);
    }

    std::string JniBridge::GetGroupName(
        const std::vector<uint8_t>& callerSessionId,
        const std::vector<uint8_t>& groupId)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return {};
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return {};
        }
        (void)principal;
        std::string name;
        if (!m_mm2.GetGroupName(groupId, name)) {
            return {};
        }
        return name;
    }

    bool JniBridge::StoreFileChunk(
        const std::vector<uint8_t>& callerSessionId,
        const std::string&          fileId,
        int                         chunkIndex,
        const std::vector<uint8_t>& data)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return false;
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return false;
        }
        (void)principal;
        if (chunkIndex < 0) {
            return false;
        }
        return m_mm2.StoreFileChunk(fileId, static_cast<uint32_t>(chunkIndex), data);
    }

    std::vector<uint8_t> JniBridge::GetFileChunk(
        const std::vector<uint8_t>& callerSessionId,
        const std::string&          fileId,
        int                         chunkIndex)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return {};
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return {};
        }
        (void)principal;
        if (chunkIndex < 0) {
            return {};
        }
        std::vector<uint8_t> out;
        if (!m_mm2.GetFileChunk(fileId, static_cast<uint32_t>(chunkIndex), out)) {
            return {};
        }
        return out;
    }

    bool JniBridge::CompleteFile(
        const std::vector<uint8_t>& callerSessionId,
        const std::string&          fileId,
        const std::vector<uint8_t>& sha256)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return false;
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return false;
        }
        (void)principal;
        if (sha256.size() != SHA256_SIZE) {
            return false;
        }
        return m_mm2.CompleteFile(fileId, sha256.data());
    }

    bool JniBridge::CancelFile(
        const std::vector<uint8_t>& callerSessionId,
        const std::string&          fileId)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return false;
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return false;
        }
        (void)principal;
        return m_mm2.CancelFile(fileId);
    }

    bool JniBridge::StoreTransferResumeChunkIndex(
        const std::vector<uint8_t>& callerSessionId,
        const std::string&          fileId,
        uint32_t                    chunkIndex)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return false;
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return false;
        }
        (void)principal;
        return m_mm2.StoreTransferResumeChunkIndex(fileId, chunkIndex);
    }

    uint32_t JniBridge::GetTransferResumeChunkIndex(
        const std::vector<uint8_t>& callerSessionId,
        const std::string&          fileId)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return UINT32_MAX;
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return UINT32_MAX;
        }
        (void)principal;
        uint32_t out = 0;
        if (!m_mm2.GetTransferResumeChunkIndex(fileId, out)) {
            return UINT32_MAX;
        }
        return out;
    }

    bool JniBridge::CleanupTransferResumeChunkIndex(
        const std::vector<uint8_t>& callerSessionId,
        const std::string&          fileId)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return false;
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return false;
        }
        (void)principal;
        return m_mm2.CleanupTransferResumeChunkIndex(fileId);
    }

    std::vector<std::vector<uint8_t>> JniBridge::GetSessionMessages(
        const std::vector<uint8_t>& callerSessionId,
        const std::vector<uint8_t>& imSessionId,
        int                         limit)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return {};
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return {};
        }
        (void)principal;
        if (imSessionId.size() != USER_ID_SIZE) {
            return {};
        }
        std::vector<std::pair<std::vector<uint8_t>, std::vector<uint8_t>>> rows;
        if (!m_mm2.GetSessionMessages(imSessionId, static_cast<size_t>(limit < 0 ? 0 : limit), rows)) {
            return {};
        }
        return PackSessionMessageRows(rows);
    }

    bool JniBridge::GetSessionStatus(
        const std::vector<uint8_t>& callerSessionId,
        const std::vector<uint8_t>& imSessionId)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return false;
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return false;
        }
        (void)principal;
        if (imSessionId.size() != USER_ID_SIZE) {
            return false;
        }
        return m_mm1.GetSessionActivityManager().GetSessionStatus(imSessionId);
    }

    void JniBridge::TouchSession(
        const std::vector<uint8_t>& callerSessionId,
        const std::vector<uint8_t>& imSessionId,
        uint64_t                    nowMs)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return;
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return;
        }
        (void)principal;
        if (imSessionId.size() != USER_ID_SIZE) {
            return;
        }
        m_mm1.GetSessionActivityManager().TouchSession(imSessionId, nowMs);
    }

    void JniBridge::CleanupExpiredSessions(
        const std::vector<uint8_t>& callerSessionId,
        uint64_t                    nowMs)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return;
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return;
        }
        (void)principal;
        m_mm1.GetSessionActivityManager().CleanupExpiredSessions(nowMs);
    }

    bool JniBridge::RegisterDeviceSession(
        const std::vector<uint8_t>& callerSessionId,
        const std::vector<uint8_t>& userId,
        const std::vector<uint8_t>& deviceId,
        const std::vector<uint8_t>& sessionId,
        uint64_t                    loginTimeMs,
        uint64_t                    lastActiveMs,
        std::vector<uint8_t>&       outKickedSessionId)
    {
        outKickedSessionId.clear();
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return false;
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return false;
        }
        if (!PrincipalMatches(principal, userId)) {
            return false;
        }
        return m_mm1.GetDeviceSessionManager().RegisterDeviceSession(
            userId,
            deviceId,
            sessionId,
            loginTimeMs,
            lastActiveMs,
            outKickedSessionId);
    }

    bool JniBridge::UpdateLastActive(
        const std::vector<uint8_t>& callerSessionId,
        const std::vector<uint8_t>& userId,
        const std::vector<uint8_t>& sessionId,
        uint64_t                    nowMs)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return false;
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return false;
        }
        if (!PrincipalMatches(principal, userId)) {
            return false;
        }
        return m_mm1.GetDeviceSessionManager().UpdateLastActive(userId, sessionId, nowMs);
    }

    std::vector<std::vector<uint8_t>> JniBridge::GetDeviceSessions(
        const std::vector<uint8_t>& callerSessionId,
        const std::vector<uint8_t>& userId)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return {};
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return {};
        }
        if (!PrincipalMatches(principal, userId)) {
            return {};
        }
        const std::vector<mm1::DeviceSessionEntry> entries =
            m_mm1.GetDeviceSessionManager().GetDeviceSessions(userId);
        std::vector<std::vector<uint8_t>>          packed = PackDeviceSessions(entries);
        if (packed.size() != entries.size()) {
            return {};
        }
        return packed;
    }

    void JniBridge::CleanupExpiredDeviceSessions(
        const std::vector<uint8_t>& callerSessionId,
        uint64_t                    nowMs)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return;
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return;
        }
        (void)principal;
        m_mm1.GetDeviceSessionManager().CleanupExpiredSessions(nowMs);
    }

    bool JniBridge::GetUserStatus(
        const std::vector<uint8_t>& callerSessionId,
        const std::vector<uint8_t>& userId)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return false;
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return false;
        }
        if (!PrincipalMatches(principal, userId)) {
            return false;
        }
        return m_mm1.GetUserStatusManager().GetUserStatus(userId);
    }

    bool JniBridge::CleanupSessionMessages(
        const std::vector<uint8_t>& callerSessionId,
        const std::vector<uint8_t>& imSessionId)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return false;
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return false;
        }
        (void)principal;
        if (imSessionId.size() != USER_ID_SIZE) {
            return false;
        }
        return m_mm2.CleanupSessionMessages(imSessionId);
    }

    bool JniBridge::CleanupExpiredData(const std::vector<uint8_t>& callerSessionId)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return false;
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return false;
        }
        (void)principal;
        return m_mm2.CleanupExpiredData();
    }

    bool JniBridge::OptimizeStorage(const std::vector<uint8_t>& callerSessionId)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return false;
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return false;
        }
        (void)principal;
        return m_mm2.OptimizeStorage();
    }

    std::map<std::string, std::string> JniBridge::GetStorageStatus(const std::vector<uint8_t>& callerSessionId)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return {};
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return {};
        }
        (void)principal;
        size_t total = 0, used = 0, avail = 0;
        if (!m_mm2.GetStorageStatus(total, used, avail)) {
            return {};
        }
        std::map<std::string, std::string> m;
        m["totalSpace"]     = std::to_string(total);
        m["usedSpace"]      = std::to_string(used);
        m["availableSpace"] = std::to_string(avail);
        return m;
    }

    int64_t JniBridge::GetMessageCount(const std::vector<uint8_t>& callerSessionId)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return -1;
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return -1;
        }
        (void)principal;
        return static_cast<int64_t>(m_mm2.GetMessageCount());
    }

    int64_t JniBridge::GetFileCount(const std::vector<uint8_t>& callerSessionId)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return -1;
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return -1;
        }
        (void)principal;
        return static_cast<int64_t>(m_mm2.GetFileCount());
    }

    std::vector<uint8_t> JniBridge::GenerateMasterKey(const std::vector<uint8_t>& callerSessionId)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return {};
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return {};
        }
        (void)principal;
        return m_mm1.GenerateMasterKey();
    }

    std::vector<uint8_t> JniBridge::RefreshSessionKey(const std::vector<uint8_t>& callerSessionId)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return {};
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return {};
        }
        (void)principal;
        return m_mm1.RefreshSessionKey();
    }

    void JniBridge::EmergencyWipe(const std::vector<uint8_t>& callerSessionId)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return;
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return;
        }
        (void)principal;
        // 高危：与 MM1::EmergencyTrustedZoneWipe / SystemControl::EmergencyWipe 同源（含 MM2 清库、认证/多设备/在线/Pin/IM 活跃/@ALL 限速、主密钥）。
        m_mm1.EmergencyTrustedZoneWipe();
        NotifyExternalTrustedZoneWipeHandled();
    }

    std::map<std::string, std::string> JniBridge::GetStatus(const std::vector<uint8_t>& callerSessionId)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return {};
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return {};
        }
        (void)principal;
        std::map<std::string, std::string> out;
        out["jni_bridge_initialized"] = "1";
        out["mm2_initialized"]        = m_mm2.IsInitialized() ? "1" : "0";
        out["mm1_master_key_present"] = m_mm1.HasMasterKey() ? "1" : "0";
        return out;
    }

    bool JniBridge::RotateKeys(const std::vector<uint8_t>& callerSessionId)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return false;
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return false;
        }
        (void)principal;
        const std::vector<uint8_t> k = m_mm1.RefreshMasterKey();
        return !k.empty();
    }

    void JniBridge::ConfigurePinnedPublicKeyHashes(
        const std::vector<uint8_t>& callerSessionId,
        const std::vector<uint8_t>& currentSpkiSha256,
        const std::vector<uint8_t>& standbySpkiSha256)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return;
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return;
        }
        (void)principal;
        m_mm1.GetCertPinningManager().ConfigurePinnedPublicKeyHashes(currentSpkiSha256, standbySpkiSha256);
    }

    bool JniBridge::VerifyPinnedServerCertificate(
        const std::vector<uint8_t>& callerSessionId,
        const std::vector<uint8_t>& clientId,
        const std::vector<uint8_t>& presentedSpkiSha256)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return false;
        }
        if (!callerSessionId.empty()) {
            std::vector<uint8_t> principal;
            if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
                return false;
            }
            (void)principal;
        }
        return m_mm1.GetCertPinningManager().VerifyPinnedServerCertificate(clientId, presentedSpkiSha256);
    }

    bool JniBridge::IsClientBanned(
        const std::vector<uint8_t>& callerSessionId,
        const std::vector<uint8_t>& clientId)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return false;
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return false;
        }
        (void)principal;
        return m_mm1.GetCertPinningManager().IsClientBanned(clientId);
    }

    void JniBridge::RecordFailure(
        const std::vector<uint8_t>& callerSessionId,
        const std::vector<uint8_t>& clientId)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return;
        }
        if (!callerSessionId.empty()) {
            std::vector<uint8_t> principal;
            if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
                return;
            }
            (void)principal;
        }
        m_mm1.GetCertPinningManager().RecordFailure(clientId);
    }

    void JniBridge::ClearBan(
        const std::vector<uint8_t>& callerSessionId,
        const std::vector<uint8_t>& clientId)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return;
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return;
        }
        (void)principal;
        m_mm1.GetCertPinningManager().ClearBan(clientId);
    }

    bool JniBridge::DeleteAccount(
        const std::vector<uint8_t>& callerSessionId,
        const std::vector<uint8_t>& userId,
        const std::vector<uint8_t>& reauthToken,
        const std::vector<uint8_t>& secondConfirmToken)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return false;
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return false;
        }
        if (!PrincipalMatches(principal, userId)) {
            return false;
        }
        return m_mm1.GetAccountDeleteManager().DeleteAccount(userId, reauthToken, secondConfirmToken);
    }

    bool JniBridge::IsAccountDeleted(
        const std::vector<uint8_t>& callerSessionId,
        const std::vector<uint8_t>& userId)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return false;
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return false;
        }
        if (!PrincipalMatches(principal, userId)) {
            return false;
        }
        return m_mm1.GetAccountDeleteManager().IsAccountDeleted(userId);
    }

    bool JniBridge::UpdateFriendNote(
        const std::vector<uint8_t>& callerSessionId,
        const std::vector<uint8_t>& userId,
        const std::vector<uint8_t>& friendId,
        const std::vector<uint8_t>& newEncryptedNote,
        uint64_t                    updateTimestampSeconds,
        const std::vector<uint8_t>& signatureEd25519)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!m_initialized.load(std::memory_order_relaxed)) {
            return false;
        }
        std::vector<uint8_t> principal;
        if (!TryBindCaller(m_mm1, callerSessionId, principal)) {
            return false;
        }
        if (!PrincipalMatches(principal, userId)) {
            return false;
        }
        return m_mm1.GetFriendNoteManager().UpdateFriendNote(
            userId,
            friendId,
            newEncryptedNote,
            updateTimestampSeconds,
            signatureEd25519);
    }

    bool JniBridge::ValidateJniCall()
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        // Weak: bridge initialized only; strong: ValidateJniCall(env, jcls).
        return m_initialized.load(std::memory_order_relaxed);
    }

    bool JniBridge::ValidateJniCall(const void* jniEnv, const void* jcls)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        return m_mm1.ValidateJniCall(jniEnv, jcls);
    }

} // namespace ZChatIM::jni
