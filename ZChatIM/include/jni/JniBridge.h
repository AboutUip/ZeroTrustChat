#pragma once

#include "../Types.h"
#include "../common/JniSecurityPolicy.h"
#include "../mm1/MM1.h"
#include "../mm2/MM2.h"
#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace ZChatIM
{
    namespace jni
    {
        // =============================================================
        // JNI 桥接类（进程内单例；由 JniInterface / jni/JniNatives 调用）
        // -------------------------------------------------------------
        // 公开方法顺序、签名须与 JniInterface.h 严格一致。
        // 契约表：docs/06-Appendix/01-JNI.md、ZChatIM/docs/JNI-API-Documentation.md
        // =============================================================

        class JniBridge {
        public:
            static JniBridge& Instance();

            // dataDir / indexDir：须非空；路由至 MM2::Initialize（见 JniSecurityPolicy / 01-JNI.md）。
            bool Initialize(const std::string& dataDir, const std::string& indexDir);
            // **`messageKeyPassphraseUtf8`**：非空指针且 C 串非空 → **`MM2::Initialize(..., passphrase)`**（**ZMKP** 等，须 **`ZCHATIM_USE_SQLCIPHER=ON`**）；**`nullptr`** 与两参数 **`Initialize`** 等价。
            bool Initialize(
                const std::string& dataDir,
                const std::string& indexDir,
                const char*        messageKeyPassphraseUtf8);
            void Cleanup();

            // =============================================================
            // 认证模块
            // =============================================================

            std::vector<uint8_t> Auth(
                const std::vector<uint8_t>& userId,
                const std::vector<uint8_t>& token,
                const std::vector<uint8_t>& clientIp = {});
            bool VerifySession(const std::vector<uint8_t>& sessionId);
            bool DestroySession(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& sessionIdToDestroy);

            bool RegisterLocalUser(
                const std::vector<uint8_t>& userId,
                const std::vector<uint8_t>& passwordUtf8,
                const std::vector<uint8_t>& recoverySecretUtf8);

            std::vector<uint8_t> AuthWithLocalPassword(
                const std::vector<uint8_t>& userId,
                const std::vector<uint8_t>& passwordUtf8,
                const std::vector<uint8_t>& clientIp = {});

            bool HasLocalPassword(const std::vector<uint8_t>& userId);

            bool ChangeLocalPassword(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& userId,
                const std::vector<uint8_t>& oldPasswordUtf8,
                const std::vector<uint8_t>& newPasswordUtf8);

            bool ResetLocalPasswordWithRecovery(
                const std::vector<uint8_t>& userId,
                const std::vector<uint8_t>& recoverySecretUtf8,
                const std::vector<uint8_t>& newPasswordUtf8,
                const std::vector<uint8_t>& clientIp = {});

            std::vector<uint8_t> RtcStartCall(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& peerUserId,
                int32_t callKind);

            bool RtcAcceptCall(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& callId);

            bool RtcRejectCall(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& callId);

            bool RtcEndCall(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& callId);

            int32_t RtcGetCallState(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& callId);

            int32_t RtcGetCallKind(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& callId);

            // =============================================================
            // 消息操作
            // =============================================================

            std::vector<uint8_t> StoreMessage(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& imSessionId,
                const std::vector<uint8_t>& payload);

            std::vector<uint8_t> RetrieveMessage(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& messageId);

            bool DeleteMessage(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& messageId,
                const std::vector<uint8_t>& senderId,
                const std::vector<uint8_t>& signatureEd25519);

            bool RecallMessage(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& messageId,
                const std::vector<uint8_t>& senderId,
                const std::vector<uint8_t>& signatureEd25519);

            std::vector<std::vector<uint8_t>> ListMessages(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& userId,
                int count);

            // =============================================================
            // 消息同步/缓存/回复/编辑
            // =============================================================

            std::vector<std::vector<uint8_t>> ListMessagesSinceTimestamp(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& userId,
                uint64_t sinceTimestampMs,
                int count);

            std::vector<std::vector<uint8_t>> ListMessagesSinceMessageId(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& userId,
                const std::vector<uint8_t>& lastMsgId,
                int count);

            bool MarkMessageRead(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& messageId,
                uint64_t readTimestampMs);

            std::vector<std::vector<uint8_t>> GetUnreadSessionMessageIds(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& imSessionId,
                int limit);

            bool StoreMessageReplyRelation(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& senderEd25519PublicKey,
                const std::vector<uint8_t>& messageId,
                const std::vector<uint8_t>& repliedMsgId,
                const std::vector<uint8_t>& repliedSenderId,
                const std::vector<uint8_t>& repliedContentDigest,
                const std::vector<uint8_t>& senderId,
                const std::vector<uint8_t>& signatureEd25519);

            std::vector<std::vector<uint8_t>> GetMessageReplyRelation(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& messageId);

            bool EditMessage(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& messageId,
                const std::vector<uint8_t>& newEncryptedContent,
                uint64_t editTimestampSeconds,
                const std::vector<uint8_t>& signature,
                const std::vector<uint8_t>& senderId);

            std::vector<uint8_t> GetMessageEditState(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& messageId);

            // =============================================================
            // 用户数据模块
            // =============================================================

            bool StoreUserData(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& userId,
                int32_t type,
                const std::vector<uint8_t>& data);

            std::vector<uint8_t> GetUserData(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& userId,
                int32_t type);

            bool DeleteUserData(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& userId,
                int32_t type);

            // =============================================================
            // 好友模块
            // =============================================================

            std::vector<uint8_t> SendFriendRequest(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& fromUserId,
                const std::vector<uint8_t>& toUserId,
                uint64_t timestampSeconds,
                const std::vector<uint8_t>& signatureEd25519);

            bool RespondFriendRequest(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& requestId,
                bool accept,
                const std::vector<uint8_t>& responderId,
                uint64_t timestampSeconds,
                const std::vector<uint8_t>& signatureEd25519);

            bool DeleteFriend(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& userId,
                const std::vector<uint8_t>& friendId,
                uint64_t timestampSeconds,
                const std::vector<uint8_t>& signatureEd25519);

            std::vector<std::vector<uint8_t>> GetFriends(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& userId);

            // =============================================================
            // 群组模块
            // =============================================================

            std::vector<uint8_t> CreateGroup(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& creatorId,
                const std::string& name);

            bool InviteMember(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& groupId,
                const std::vector<uint8_t>& userId);

            bool RemoveMember(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& groupId,
                const std::vector<uint8_t>& userId);

            bool LeaveGroup(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& groupId,
                const std::vector<uint8_t>& userId);

            std::vector<std::vector<uint8_t>> GetGroupMembers(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& groupId);

            bool UpdateGroupKey(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& groupId);

            // =============================================================
            // 群聊安全特性
            // =============================================================

            bool ValidateMentionRequest(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& groupId,
                const std::vector<uint8_t>& senderId,
                int32_t mentionType,
                const std::vector<std::vector<uint8_t>>& mentionedUserIds,
                uint64_t nowMs,
                const std::vector<uint8_t>& signatureEd25519);

            bool RecordMentionAtAllUsage(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& groupId,
                const std::vector<uint8_t>& senderId,
                uint64_t nowMs);

            bool MuteMember(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& groupId,
                const std::vector<uint8_t>& userId,
                const std::vector<uint8_t>& mutedBy,
                uint64_t startTimeMs,
                int64_t durationSeconds,
                const std::vector<uint8_t>& reason);

            bool IsMuted(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& groupId,
                const std::vector<uint8_t>& userId,
                uint64_t nowMs);

            bool UnmuteMember(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& groupId,
                const std::vector<uint8_t>& userId,
                const std::vector<uint8_t>& unmutedBy);

            bool UpdateGroupName(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& groupId,
                const std::vector<uint8_t>& updaterId,
                const std::string& newGroupName,
                uint64_t nowMs);

            std::string GetGroupName(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& groupId);

            // =============================================================
            // 文件操作
            // =============================================================

            bool StoreFileChunk(
                const std::vector<uint8_t>& callerSessionId,
                const std::string& fileId,
                int chunkIndex,
                const std::vector<uint8_t>& data);

            std::vector<uint8_t> GetFileChunk(
                const std::vector<uint8_t>& callerSessionId,
                const std::string& fileId,
                int chunkIndex);

            bool CompleteFile(
                const std::vector<uint8_t>& callerSessionId,
                const std::string& fileId,
                const std::vector<uint8_t>& sha256);

            bool CancelFile(
                const std::vector<uint8_t>& callerSessionId,
                const std::string& fileId);

            bool StoreTransferResumeChunkIndex(
                const std::vector<uint8_t>& callerSessionId,
                const std::string& fileId,
                uint32_t chunkIndex);

            uint32_t GetTransferResumeChunkIndex(
                const std::vector<uint8_t>& callerSessionId,
                const std::string& fileId);

            bool CleanupTransferResumeChunkIndex(
                const std::vector<uint8_t>& callerSessionId,
                const std::string& fileId);

            // =============================================================
            // 会话管理
            // =============================================================

            std::vector<std::vector<uint8_t>> GetSessionMessages(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& imSessionId,
                int limit);

            bool GetSessionStatus(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& imSessionId);

            void TouchSession(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& imSessionId,
                uint64_t nowMs);

            void CleanupExpiredSessions(
                const std::vector<uint8_t>& callerSessionId,
                uint64_t nowMs);

            // =============================================================
            // 多设备登录
            // =============================================================

            // 返回 true 且 outKickedSessionId 为空：成功、无设备被踢；非空 16B：被踢出的 device 会话 id。
            // 返回 false：参数/会话/初始化失败（与「成功无踢」区分；JNI 映射为 Java null）。
            bool RegisterDeviceSession(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& userId,
                const std::vector<uint8_t>& deviceId,
                const std::vector<uint8_t>& sessionId,
                uint64_t                    loginTimeMs,
                uint64_t                    lastActiveMs,
                std::vector<uint8_t>&       outKickedSessionId);

            bool UpdateLastActive(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& userId,
                const std::vector<uint8_t>& sessionId,
                uint64_t nowMs);

            std::vector<std::vector<uint8_t>> GetDeviceSessions(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& userId);

            void CleanupExpiredDeviceSessions(
                const std::vector<uint8_t>& callerSessionId,
                uint64_t nowMs);

            bool GetUserStatus(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& userId);

            bool CleanupSessionMessages(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& imSessionId);

            // =============================================================
            // 数据清理
            // =============================================================

            bool CleanupExpiredData(const std::vector<uint8_t>& callerSessionId);
            bool OptimizeStorage(const std::vector<uint8_t>& callerSessionId);

            // =============================================================
            // 状态查询
            // =============================================================

            std::map<std::string, std::string> GetStorageStatus(const std::vector<uint8_t>& callerSessionId);
            int64_t GetMessageCount(const std::vector<uint8_t>& callerSessionId);
            int64_t GetFileCount(const std::vector<uint8_t>& callerSessionId);

            // =============================================================
            // 安全操作
            // =============================================================

            std::vector<uint8_t> GenerateMasterKey(const std::vector<uint8_t>& callerSessionId);
            std::vector<uint8_t> RefreshSessionKey(const std::vector<uint8_t>& callerSessionId);
            void EmergencyWipe(const std::vector<uint8_t>& callerSessionId);
            std::map<std::string, std::string> GetStatus(const std::vector<uint8_t>& callerSessionId);
            bool RotateKeys(const std::vector<uint8_t>& callerSessionId);

            // =============================================================
            // 证书固定
            // =============================================================

            void ConfigurePinnedPublicKeyHashes(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& currentSpkiSha256,
                const std::vector<uint8_t>& standbySpkiSha256);

            bool VerifyPinnedServerCertificate(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& clientId,
                const std::vector<uint8_t>& presentedSpkiSha256);

            bool IsClientBanned(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& clientId);

            void RecordFailure(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& clientId);

            void ClearBan(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& clientId);

            bool DeleteAccount(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& userId,
                const std::vector<uint8_t>& reauthToken,
                const std::vector<uint8_t>& secondConfirmToken);

            bool IsAccountDeleted(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& userId);

            bool UpdateFriendNote(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& userId,
                const std::vector<uint8_t>& friendId,
                const std::vector<uint8_t>& newEncryptedNote,
                uint64_t updateTimestampSeconds,
                const std::vector<uint8_t>& signatureEd25519);

            bool ValidateJniCall();
            bool ValidateJniCall(const void* jniEnv, const void* jclass);

        private:
            JniBridge();
            ~JniBridge();

            JniBridge(const JniBridge&) = delete;
            JniBridge& operator=(const JniBridge&) = delete;

            bool CheckInitialized();
            void LogOperation(const std::string& operation, bool success);

            // **`CheckInitialized()`** 无桥接锁读取；须 **atomic** 与 **`Initialize`/`Cleanup`/`EmergencyWipe`** 的 store **release/acquire** 成对，避免数据竞争。
            std::atomic<bool> m_initialized{false};
            mutable std::recursive_mutex m_apiRecursiveMutex;

            mm1::MM1& m_mm1;
            mm2::MM2& m_mm2;
        };

    } // namespace jni
} // namespace ZChatIM
