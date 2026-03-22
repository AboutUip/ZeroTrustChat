#pragma once

#include <map>
#include "../Types.h"
#include "../common/JniSecurityPolicy.h"

namespace ZChatIM
{
    namespace jni
    {
        // =============================================================
        // JNI 接口（C++ 侧契约；Java 经 jni/JniNatives.cpp RegisterNatives 绑定）
        // -------------------------------------------------------------
        // 最高安全性契约：除 Initialize / Cleanup / Auth / VerifySession /
        // ValidateJniCall* 外，业务 API 首参均为 callerSessionId
        // （长度 Types::JNI_AUTH_SESSION_TOKEN_BYTES，与 Auth 返回句柄一致）。
        // 会话有效：JniBridge 用 TryGetSessionUserId(caller, principal)，与 VerifySession 等价。
        // principal 与后续 id 的绑定按 API 分流（非一律 imSessionId==principal），权威表：
        // docs/06-Appendix/01-JNI.md「principal 绑定矩阵」；摘要：common/JniSecurityPolicy.h
        //
        // 契约表顺序：docs/06-Appendix/01-JNI.md、ZChatIM/docs/JNI-API-Documentation.md
        // 实例桥接：JniBridge.h（同序同签名，无 static）
        // =============================================================

        class JniInterface {
        public:
            // =============================================================
            // 初始化
            // =============================================================

            static bool Initialize(const std::string& dataDir, const std::string& indexDir);
            static void Cleanup();

            // =============================================================
            // 认证模块（无 callerSessionId：建立/校验会话本身）
            // =============================================================

            // clientIp：可选；非空时启用 IP 级限流并与 userId 组合封禁键（见 docs/03-Business/02-Auth.md、AuthSessionManager）。
            static std::vector<uint8_t> Auth(
                const std::vector<uint8_t>& userId,
                const std::vector<uint8_t>& token,
                const std::vector<uint8_t>& clientIp = {});
            static bool VerifySession(const std::vector<uint8_t>& sessionId);
            static bool DestroySession(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& sessionIdToDestroy);

            // =============================================================
            // 消息操作
            // =============================================================

            static std::vector<uint8_t> StoreMessage(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& imSessionId,
                const std::vector<uint8_t>& payload);

            static std::vector<uint8_t> RetrieveMessage(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& messageId);

            static bool DeleteMessage(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& messageId,
                const std::vector<uint8_t>& senderId,
                const std::vector<uint8_t>& signatureEd25519);

            static bool RecallMessage(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& messageId,
                const std::vector<uint8_t>& senderId,
                const std::vector<uint8_t>& signatureEd25519);

            static std::vector<std::vector<uint8_t>> ListMessages(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& userId,
                int count);

            // =============================================================
            // 消息同步/缓存/回复/编辑
            // =============================================================

            static std::vector<std::vector<uint8_t>> ListMessagesSinceTimestamp(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& userId,
                uint64_t sinceTimestampMs,
                int count);

            static std::vector<std::vector<uint8_t>> ListMessagesSinceMessageId(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& userId,
                const std::vector<uint8_t>& lastMsgId,
                int count);

            static bool MarkMessageRead(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& messageId,
                uint64_t readTimestampMs);

            static std::vector<std::vector<uint8_t>> GetUnreadSessionMessageIds(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& imSessionId,
                int limit);

            static bool StoreMessageReplyRelation(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& senderEd25519PublicKey,
                const std::vector<uint8_t>& messageId,
                const std::vector<uint8_t>& repliedMsgId,
                const std::vector<uint8_t>& repliedSenderId,
                const std::vector<uint8_t>& repliedContentDigest,
                const std::vector<uint8_t>& senderId,
                const std::vector<uint8_t>& signatureEd25519);

            static std::vector<std::vector<uint8_t>> GetMessageReplyRelation(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& messageId);

            static bool EditMessage(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& messageId,
                const std::vector<uint8_t>& newEncryptedContent,
                uint64_t editTimestampSeconds,
                const std::vector<uint8_t>& signature,
                const std::vector<uint8_t>& senderId);

            static std::vector<uint8_t> GetMessageEditState(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& messageId);

            // =============================================================
            // 用户数据模块
            // =============================================================

            static bool StoreUserData(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& userId,
                int32_t type,
                const std::vector<uint8_t>& data);

            static std::vector<uint8_t> GetUserData(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& userId,
                int32_t type);

            static bool DeleteUserData(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& userId,
                int32_t type);

            // =============================================================
            // 好友模块
            // =============================================================

            static std::vector<uint8_t> SendFriendRequest(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& fromUserId,
                const std::vector<uint8_t>& toUserId,
                uint64_t timestampSeconds,
                const std::vector<uint8_t>& signatureEd25519);

            static bool RespondFriendRequest(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& requestId,
                bool accept,
                const std::vector<uint8_t>& responderId,
                uint64_t timestampSeconds,
                const std::vector<uint8_t>& signatureEd25519);

            static bool DeleteFriend(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& userId,
                const std::vector<uint8_t>& friendId,
                uint64_t timestampSeconds,
                const std::vector<uint8_t>& signatureEd25519);

            static std::vector<std::vector<uint8_t>> GetFriends(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& userId);

            // =============================================================
            // 群组模块
            // =============================================================

            static std::vector<uint8_t> CreateGroup(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& creatorId,
                const std::string& name);

            static bool InviteMember(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& groupId,
                const std::vector<uint8_t>& userId);

            static bool RemoveMember(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& groupId,
                const std::vector<uint8_t>& userId);

            static bool LeaveGroup(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& groupId,
                const std::vector<uint8_t>& userId);

            static std::vector<std::vector<uint8_t>> GetGroupMembers(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& groupId);

            static bool UpdateGroupKey(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& groupId);

            // =============================================================
            // 群聊安全特性：@提及、群禁言、群名称修改
            // =============================================================

            static bool ValidateMentionRequest(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& groupId,
                const std::vector<uint8_t>& senderId,
                int32_t mentionType,
                const std::vector<std::vector<uint8_t>>& mentionedUserIds,
                uint64_t nowMs,
                const std::vector<uint8_t>& signatureEd25519);

            static bool RecordMentionAtAllUsage(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& groupId,
                const std::vector<uint8_t>& senderId,
                uint64_t nowMs);

            static bool MuteMember(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& groupId,
                const std::vector<uint8_t>& userId,
                const std::vector<uint8_t>& mutedBy,
                uint64_t startTimeMs,
                int64_t durationSeconds,
                const std::vector<uint8_t>& reason);

            static bool IsMuted(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& groupId,
                const std::vector<uint8_t>& userId,
                uint64_t nowMs);

            static bool UnmuteMember(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& groupId,
                const std::vector<uint8_t>& userId,
                const std::vector<uint8_t>& unmutedBy);

            static bool UpdateGroupName(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& groupId,
                const std::vector<uint8_t>& updaterId,
                const std::string& newGroupName,
                uint64_t nowMs);

            static std::string GetGroupName(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& groupId);

            // =============================================================
            // 文件操作
            // =============================================================

            static bool StoreFileChunk(
                const std::vector<uint8_t>& callerSessionId,
                const std::string& fileId,
                int chunkIndex,
                const std::vector<uint8_t>& data);

            static std::vector<uint8_t> GetFileChunk(
                const std::vector<uint8_t>& callerSessionId,
                const std::string& fileId,
                int chunkIndex);

            static bool CompleteFile(
                const std::vector<uint8_t>& callerSessionId,
                const std::string& fileId,
                const std::vector<uint8_t>& sha256);

            static bool CancelFile(
                const std::vector<uint8_t>& callerSessionId,
                const std::string& fileId);

            static bool StoreTransferResumeChunkIndex(
                const std::vector<uint8_t>& callerSessionId,
                const std::string& fileId,
                uint32_t chunkIndex);

            static uint32_t GetTransferResumeChunkIndex(
                const std::vector<uint8_t>& callerSessionId,
                const std::string& fileId);

            static bool CleanupTransferResumeChunkIndex(
                const std::vector<uint8_t>& callerSessionId,
                const std::string& fileId);

            // =============================================================
            // 会话管理
            // =============================================================

            static std::vector<std::vector<uint8_t>> GetSessionMessages(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& imSessionId,
                int limit);

            static bool GetSessionStatus(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& imSessionId);

            static void TouchSession(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& imSessionId,
                uint64_t nowMs);

            static void CleanupExpiredSessions(
                const std::vector<uint8_t>& callerSessionId,
                uint64_t nowMs);

            // =============================================================
            // 多设备登录
            // =============================================================

            // 语义同 JniBridge::RegisterDeviceSession（out 参数区分失败 vs 成功无踢）。
            static bool RegisterDeviceSession(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& userId,
                const std::vector<uint8_t>& deviceId,
                const std::vector<uint8_t>& sessionId,
                uint64_t                    loginTimeMs,
                uint64_t                    lastActiveMs,
                std::vector<uint8_t>&       outKickedSessionId);

            static bool UpdateLastActive(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& userId,
                const std::vector<uint8_t>& sessionId,
                uint64_t nowMs);

            static std::vector<std::vector<uint8_t>> GetDeviceSessions(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& userId);

            static void CleanupExpiredDeviceSessions(
                const std::vector<uint8_t>& callerSessionId,
                uint64_t nowMs);

            static bool GetUserStatus(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& userId);

            static bool CleanupSessionMessages(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& imSessionId);

            // =============================================================
            // 数据清理
            // =============================================================

            static bool CleanupExpiredData(const std::vector<uint8_t>& callerSessionId);
            static bool OptimizeStorage(const std::vector<uint8_t>& callerSessionId);

            // =============================================================
            // 状态查询
            // =============================================================

            static std::map<std::string, std::string> GetStorageStatus(const std::vector<uint8_t>& callerSessionId);
            static int64_t GetMessageCount(const std::vector<uint8_t>& callerSessionId);
            static int64_t GetFileCount(const std::vector<uint8_t>& callerSessionId);

            // =============================================================
            // 安全操作
            // =============================================================

            static std::vector<uint8_t> GenerateMasterKey(const std::vector<uint8_t>& callerSessionId);
            static std::vector<uint8_t> RefreshSessionKey(const std::vector<uint8_t>& callerSessionId);
            static void EmergencyWipe(const std::vector<uint8_t>& callerSessionId);
            static std::map<std::string, std::string> GetStatus(const std::vector<uint8_t>& callerSessionId);
            static bool RotateKeys(const std::vector<uint8_t>& callerSessionId);

            // =============================================================
            // 证书固定（SPKI SHA-256 Pin）
            // =============================================================

            static void ConfigurePinnedPublicKeyHashes(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& currentSpkiSha256,
                const std::vector<uint8_t>& standbySpkiSha256);

            static bool VerifyPinnedServerCertificate(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& clientId,
                const std::vector<uint8_t>& presentedSpkiSha256);

            static bool IsClientBanned(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& clientId);

            static void RecordFailure(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& clientId);

            static void ClearBan(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& clientId);

            // =============================================================
            // AccountDelete / FriendNote
            // =============================================================

            static bool DeleteAccount(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& userId,
                const std::vector<uint8_t>& reauthToken,
                const std::vector<uint8_t>& secondConfirmToken);

            static bool IsAccountDeleted(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& userId);

            static bool UpdateFriendNote(
                const std::vector<uint8_t>& callerSessionId,
                const std::vector<uint8_t>& userId,
                const std::vector<uint8_t>& friendId,
                const std::vector<uint8_t>& newEncryptedNote,
                uint64_t updateTimestampSeconds,
                const std::vector<uint8_t>& signatureEd25519);

            // =============================================================
            // JNI 调用验证（无 callerSessionId）
            // =============================================================

            static bool ValidateJniCall();
            static bool ValidateJniCall(const void* jniEnv, const void* jclass);

        private:
            JniInterface() = delete;
            ~JniInterface() = delete;
        };

    } // namespace jni
} // namespace ZChatIM
