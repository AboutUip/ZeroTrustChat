#include "jni/JniInterface.h"

#include "jni/JniBridge.h"

namespace ZChatIM::jni {

    // std::map<std::string, std::string> 含逗号，不能作为 ZCHATIM_JNI_FWD* 的 Ret 实参（预处理器会拆参）。
    using JniMapStringString = std::map<std::string, std::string>;

    bool JniInterface::Initialize(const std::string& dataDir, const std::string& indexDir)
    {
        return JniBridge::Instance().Initialize(dataDir, indexDir);
    }

    bool JniInterface::InitializeWithPassphrase(
        const std::string& dataDir,
        const std::string& indexDir,
        const char*        messageKeyPassphraseUtf8)
    {
        return JniBridge::Instance().Initialize(dataDir, indexDir, messageKeyPassphraseUtf8);
    }

    std::string JniInterface::LastInitializeError()
    {
        return JniBridge::Instance().LastInitializeError();
    }

    void JniInterface::Cleanup()
    {
        JniBridge::Instance().Cleanup();
    }

    void JniInterface::NotifyExternalTrustedZoneWipeHandled()
    {
        JniBridge::Instance().NotifyExternalTrustedZoneWipeHandled();
    }

#define ZCHATIM_JNI_FWD0(Ret, Name) \
    Ret JniInterface::Name() \
    { \
        return JniBridge::Instance().Name(); \
    }

#define ZCHATIM_JNI_FWD1(Ret, Name, T1, a1) \
    Ret JniInterface::Name(T1 a1) \
    { \
        return JniBridge::Instance().Name(a1); \
    }

#define ZCHATIM_JNI_FWD2(Ret, Name, T1, a1, T2, a2) \
    Ret JniInterface::Name(T1 a1, T2 a2) \
    { \
        return JniBridge::Instance().Name(a1, a2); \
    }

#define ZCHATIM_JNI_FWD3(Ret, Name, T1, a1, T2, a2, T3, a3) \
    Ret JniInterface::Name(T1 a1, T2 a2, T3 a3) \
    { \
        return JniBridge::Instance().Name(a1, a2, a3); \
    }

#define ZCHATIM_JNI_FWD4(Ret, Name, T1, a1, T2, a2, T3, a3, T4, a4) \
    Ret JniInterface::Name(T1 a1, T2 a2, T3 a3, T4 a4) \
    { \
        return JniBridge::Instance().Name(a1, a2, a3, a4); \
    }

#define ZCHATIM_JNI_FWD5(Ret, Name, T1, a1, T2, a2, T3, a3, T4, a4, T5, a5) \
    Ret JniInterface::Name(T1 a1, T2 a2, T3 a3, T4 a4, T5 a5) \
    { \
        return JniBridge::Instance().Name(a1, a2, a3, a4, a5); \
    }

#define ZCHATIM_JNI_FWD6(Ret, Name, T1, a1, T2, a2, T3, a3, T4, a4, T5, a5, T6, a6) \
    Ret JniInterface::Name(T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6) \
    { \
        return JniBridge::Instance().Name(a1, a2, a3, a4, a5, a6); \
    }

#define ZCHATIM_JNI_FWD7(Ret, Name, T1, a1, T2, a2, T3, a3, T4, a4, T5, a5, T6, a6, T7, a7) \
    Ret JniInterface::Name(T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6, T7 a7) \
    { \
        return JniBridge::Instance().Name(a1, a2, a3, a4, a5, a6, a7); \
    }

#define ZCHATIM_JNI_FWD8(Ret, Name, T1, a1, T2, a2, T3, a3, T4, a4, T5, a5, T6, a6, T7, a7, T8, a8) \
    Ret JniInterface::Name(T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6, T7 a7, T8 a8) \
    { \
        return JniBridge::Instance().Name(a1, a2, a3, a4, a5, a6, a7, a8); \
    }

    ZCHATIM_JNI_FWD3(std::vector<uint8_t>, Auth, const std::vector<uint8_t>&, userId, const std::vector<uint8_t>&, token, const std::vector<uint8_t>&, clientIp)
    ZCHATIM_JNI_FWD1(bool, VerifySession, const std::vector<uint8_t>&, sessionId)
    ZCHATIM_JNI_FWD2(bool, DestroySession, const std::vector<uint8_t>&, callerSessionId, const std::vector<uint8_t>&, sessionIdToDestroy)

    ZCHATIM_JNI_FWD3(bool, RegisterLocalUser, const std::vector<uint8_t>&, userId, const std::vector<uint8_t>&, passwordUtf8, const std::vector<uint8_t>&, recoverySecretUtf8)
    ZCHATIM_JNI_FWD3(std::vector<uint8_t>, AuthWithLocalPassword, const std::vector<uint8_t>&, userId, const std::vector<uint8_t>&, passwordUtf8, const std::vector<uint8_t>&, clientIp)
    ZCHATIM_JNI_FWD1(bool, HasLocalPassword, const std::vector<uint8_t>&, userId)
    ZCHATIM_JNI_FWD4(bool, ChangeLocalPassword, const std::vector<uint8_t>&, callerSessionId, const std::vector<uint8_t>&, userId, const std::vector<uint8_t>&, oldPasswordUtf8, const std::vector<uint8_t>&, newPasswordUtf8)
    ZCHATIM_JNI_FWD4(bool, ResetLocalPasswordWithRecovery, const std::vector<uint8_t>&, userId, const std::vector<uint8_t>&, recoverySecretUtf8, const std::vector<uint8_t>&, newPasswordUtf8, const std::vector<uint8_t>&, clientIp)

    ZCHATIM_JNI_FWD3(std::vector<uint8_t>, RtcStartCall, const std::vector<uint8_t>&, callerSessionId, const std::vector<uint8_t>&, peerUserId, int32_t, callKind)
    ZCHATIM_JNI_FWD2(bool, RtcAcceptCall, const std::vector<uint8_t>&, callerSessionId, const std::vector<uint8_t>&, callId)
    ZCHATIM_JNI_FWD2(bool, RtcRejectCall, const std::vector<uint8_t>&, callerSessionId, const std::vector<uint8_t>&, callId)
    ZCHATIM_JNI_FWD2(bool, RtcEndCall, const std::vector<uint8_t>&, callerSessionId, const std::vector<uint8_t>&, callId)
    ZCHATIM_JNI_FWD2(int32_t, RtcGetCallState, const std::vector<uint8_t>&, callerSessionId, const std::vector<uint8_t>&, callId)
    ZCHATIM_JNI_FWD2(int32_t, RtcGetCallKind, const std::vector<uint8_t>&, callerSessionId, const std::vector<uint8_t>&, callId)

    ZCHATIM_JNI_FWD3(std::vector<uint8_t>, StoreMessage, const std::vector<uint8_t>&, callerSessionId, const std::vector<uint8_t>&, imSessionId, const std::vector<uint8_t>&, payload)
    ZCHATIM_JNI_FWD2(std::vector<uint8_t>, RetrieveMessage, const std::vector<uint8_t>&, callerSessionId, const std::vector<uint8_t>&, messageId)
    ZCHATIM_JNI_FWD4(bool, DeleteMessage, const std::vector<uint8_t>&, callerSessionId, const std::vector<uint8_t>&, messageId, const std::vector<uint8_t>&, senderId, const std::vector<uint8_t>&, signatureEd25519)
    ZCHATIM_JNI_FWD4(bool, RecallMessage, const std::vector<uint8_t>&, callerSessionId, const std::vector<uint8_t>&, messageId, const std::vector<uint8_t>&, senderId, const std::vector<uint8_t>&, signatureEd25519)
    ZCHATIM_JNI_FWD3(std::vector<std::vector<uint8_t>>, ListMessages, const std::vector<uint8_t>&, callerSessionId, const std::vector<uint8_t>&, userId, int, count)
    ZCHATIM_JNI_FWD4(std::vector<std::vector<uint8_t>>, ListMessagesSinceTimestamp, const std::vector<uint8_t>&, callerSessionId, const std::vector<uint8_t>&, userId, uint64_t, sinceTimestampMs, int, count)
    ZCHATIM_JNI_FWD4(std::vector<std::vector<uint8_t>>, ListMessagesSinceMessageId, const std::vector<uint8_t>&, callerSessionId, const std::vector<uint8_t>&, userId, const std::vector<uint8_t>&, lastMsgId, int, count)
    ZCHATIM_JNI_FWD3(bool, MarkMessageRead, const std::vector<uint8_t>&, callerSessionId, const std::vector<uint8_t>&, messageId, uint64_t, readTimestampMs)
    ZCHATIM_JNI_FWD3(std::vector<std::vector<uint8_t>>, GetUnreadSessionMessageIds, const std::vector<uint8_t>&, callerSessionId, const std::vector<uint8_t>&, imSessionId, int, limit)
    ZCHATIM_JNI_FWD8(bool, StoreMessageReplyRelation, const std::vector<uint8_t>&, callerSessionId, const std::vector<uint8_t>&, senderEd25519PublicKey, const std::vector<uint8_t>&, messageId, const std::vector<uint8_t>&, repliedMsgId, const std::vector<uint8_t>&, repliedSenderId, const std::vector<uint8_t>&, repliedContentDigest, const std::vector<uint8_t>&, senderId, const std::vector<uint8_t>&, signatureEd25519)
    ZCHATIM_JNI_FWD2(std::vector<std::vector<uint8_t>>, GetMessageReplyRelation, const std::vector<uint8_t>&, callerSessionId, const std::vector<uint8_t>&, messageId)
    ZCHATIM_JNI_FWD6(bool, EditMessage, const std::vector<uint8_t>&, callerSessionId, const std::vector<uint8_t>&, messageId, const std::vector<uint8_t>&, newEncryptedContent, uint64_t, editTimestampSeconds, const std::vector<uint8_t>&, signature, const std::vector<uint8_t>&, senderId)
    ZCHATIM_JNI_FWD2(std::vector<uint8_t>, GetMessageEditState, const std::vector<uint8_t>&, callerSessionId, const std::vector<uint8_t>&, messageId)

    ZCHATIM_JNI_FWD4(bool, StoreUserData, const std::vector<uint8_t>&, callerSessionId, const std::vector<uint8_t>&, userId, int32_t, type, const std::vector<uint8_t>&, data)
    ZCHATIM_JNI_FWD3(std::vector<uint8_t>, GetUserData, const std::vector<uint8_t>&, callerSessionId, const std::vector<uint8_t>&, userId, int32_t, type)
    ZCHATIM_JNI_FWD3(bool, DeleteUserData, const std::vector<uint8_t>&, callerSessionId, const std::vector<uint8_t>&, userId, int32_t, type)

    ZCHATIM_JNI_FWD5(std::vector<uint8_t>, SendFriendRequest, const std::vector<uint8_t>&, callerSessionId, const std::vector<uint8_t>&, fromUserId, const std::vector<uint8_t>&, toUserId, uint64_t, timestampSeconds, const std::vector<uint8_t>&, signatureEd25519)
    ZCHATIM_JNI_FWD6(bool, RespondFriendRequest, const std::vector<uint8_t>&, callerSessionId, const std::vector<uint8_t>&, requestId, bool, accept, const std::vector<uint8_t>&, responderId, uint64_t, timestampSeconds, const std::vector<uint8_t>&, signatureEd25519)
    ZCHATIM_JNI_FWD5(bool, DeleteFriend, const std::vector<uint8_t>&, callerSessionId, const std::vector<uint8_t>&, userId, const std::vector<uint8_t>&, friendId, uint64_t, timestampSeconds, const std::vector<uint8_t>&, signatureEd25519)
    ZCHATIM_JNI_FWD2(std::vector<std::vector<uint8_t>>, GetFriends, const std::vector<uint8_t>&, callerSessionId, const std::vector<uint8_t>&, userId)
    ZCHATIM_JNI_FWD2(std::vector<std::vector<uint8_t>>, ListPendingFriendRequests, const std::vector<uint8_t>&, callerSessionId, const std::vector<uint8_t>&, userId)

    ZCHATIM_JNI_FWD3(std::vector<uint8_t>, CreateGroup, const std::vector<uint8_t>&, callerSessionId, const std::vector<uint8_t>&, creatorId, const std::string&, name)
    ZCHATIM_JNI_FWD3(bool, InviteMember, const std::vector<uint8_t>&, callerSessionId, const std::vector<uint8_t>&, groupId, const std::vector<uint8_t>&, userId)
    ZCHATIM_JNI_FWD3(bool, RemoveMember, const std::vector<uint8_t>&, callerSessionId, const std::vector<uint8_t>&, groupId, const std::vector<uint8_t>&, userId)
    ZCHATIM_JNI_FWD3(bool, LeaveGroup, const std::vector<uint8_t>&, callerSessionId, const std::vector<uint8_t>&, groupId, const std::vector<uint8_t>&, userId)
    ZCHATIM_JNI_FWD2(std::vector<std::vector<uint8_t>>, GetGroupMembers, const std::vector<uint8_t>&, callerSessionId, const std::vector<uint8_t>&, groupId)
    ZCHATIM_JNI_FWD2(bool, UpdateGroupKey, const std::vector<uint8_t>&, callerSessionId, const std::vector<uint8_t>&, groupId)

    ZCHATIM_JNI_FWD7(bool, ValidateMentionRequest, const std::vector<uint8_t>&, callerSessionId, const std::vector<uint8_t>&, groupId, const std::vector<uint8_t>&, senderId, int32_t, mentionType, const std::vector<std::vector<uint8_t>>&, mentionedUserIds, uint64_t, nowMs, const std::vector<uint8_t>&, signatureEd25519)
    ZCHATIM_JNI_FWD4(bool, RecordMentionAtAllUsage, const std::vector<uint8_t>&, callerSessionId, const std::vector<uint8_t>&, groupId, const std::vector<uint8_t>&, senderId, uint64_t, nowMs)
    ZCHATIM_JNI_FWD7(bool, MuteMember, const std::vector<uint8_t>&, callerSessionId, const std::vector<uint8_t>&, groupId, const std::vector<uint8_t>&, userId, const std::vector<uint8_t>&, mutedBy, uint64_t, startTimeMs, int64_t, durationSeconds, const std::vector<uint8_t>&, reason)
    ZCHATIM_JNI_FWD4(bool, IsMuted, const std::vector<uint8_t>&, callerSessionId, const std::vector<uint8_t>&, groupId, const std::vector<uint8_t>&, userId, uint64_t, nowMs)
    ZCHATIM_JNI_FWD4(bool, UnmuteMember, const std::vector<uint8_t>&, callerSessionId, const std::vector<uint8_t>&, groupId, const std::vector<uint8_t>&, userId, const std::vector<uint8_t>&, unmutedBy)
    ZCHATIM_JNI_FWD5(bool, UpdateGroupName, const std::vector<uint8_t>&, callerSessionId, const std::vector<uint8_t>&, groupId, const std::vector<uint8_t>&, updaterId, const std::string&, newGroupName, uint64_t, nowMs)
    ZCHATIM_JNI_FWD2(std::string, GetGroupName, const std::vector<uint8_t>&, callerSessionId, const std::vector<uint8_t>&, groupId)

    ZCHATIM_JNI_FWD4(bool, StoreFileChunk, const std::vector<uint8_t>&, callerSessionId, const std::string&, fileId, int, chunkIndex, const std::vector<uint8_t>&, data)
    ZCHATIM_JNI_FWD3(std::vector<uint8_t>, GetFileChunk, const std::vector<uint8_t>&, callerSessionId, const std::string&, fileId, int, chunkIndex)
    ZCHATIM_JNI_FWD3(bool, CompleteFile, const std::vector<uint8_t>&, callerSessionId, const std::string&, fileId, const std::vector<uint8_t>&, sha256)
    ZCHATIM_JNI_FWD2(bool, CancelFile, const std::vector<uint8_t>&, callerSessionId, const std::string&, fileId)
    ZCHATIM_JNI_FWD3(bool, StoreTransferResumeChunkIndex, const std::vector<uint8_t>&, callerSessionId, const std::string&, fileId, uint32_t, chunkIndex)
    ZCHATIM_JNI_FWD2(uint32_t, GetTransferResumeChunkIndex, const std::vector<uint8_t>&, callerSessionId, const std::string&, fileId)
    ZCHATIM_JNI_FWD2(bool, CleanupTransferResumeChunkIndex, const std::vector<uint8_t>&, callerSessionId, const std::string&, fileId)

    ZCHATIM_JNI_FWD3(std::vector<std::vector<uint8_t>>, GetSessionMessages, const std::vector<uint8_t>&, callerSessionId, const std::vector<uint8_t>&, imSessionId, int, limit)
    ZCHATIM_JNI_FWD2(bool, GetSessionStatus, const std::vector<uint8_t>&, callerSessionId, const std::vector<uint8_t>&, imSessionId)
    ZCHATIM_JNI_FWD3(void, TouchSession, const std::vector<uint8_t>&, callerSessionId, const std::vector<uint8_t>&, imSessionId, uint64_t, nowMs)
    ZCHATIM_JNI_FWD2(void, CleanupExpiredSessions, const std::vector<uint8_t>&, callerSessionId, uint64_t, nowMs)

    bool JniInterface::RegisterDeviceSession(
        const std::vector<uint8_t>& callerSessionId,
        const std::vector<uint8_t>& userId,
        const std::vector<uint8_t>& deviceId,
        const std::vector<uint8_t>& sessionId,
        uint64_t                    loginTimeMs,
        uint64_t                    lastActiveMs,
        std::vector<uint8_t>&       outKickedSessionId)
    {
        return JniBridge::Instance().RegisterDeviceSession(
            callerSessionId,
            userId,
            deviceId,
            sessionId,
            loginTimeMs,
            lastActiveMs,
            outKickedSessionId);
    }
    ZCHATIM_JNI_FWD4(bool, UpdateLastActive, const std::vector<uint8_t>&, callerSessionId, const std::vector<uint8_t>&, userId, const std::vector<uint8_t>&, sessionId, uint64_t, nowMs)
    ZCHATIM_JNI_FWD2(std::vector<std::vector<uint8_t>>, GetDeviceSessions, const std::vector<uint8_t>&, callerSessionId, const std::vector<uint8_t>&, userId)
    ZCHATIM_JNI_FWD2(void, CleanupExpiredDeviceSessions, const std::vector<uint8_t>&, callerSessionId, uint64_t, nowMs)
    ZCHATIM_JNI_FWD2(bool, GetUserStatus, const std::vector<uint8_t>&, callerSessionId, const std::vector<uint8_t>&, userId)
    ZCHATIM_JNI_FWD2(bool, CleanupSessionMessages, const std::vector<uint8_t>&, callerSessionId, const std::vector<uint8_t>&, imSessionId)

    ZCHATIM_JNI_FWD1(bool, CleanupExpiredData, const std::vector<uint8_t>&, callerSessionId)
    ZCHATIM_JNI_FWD1(bool, OptimizeStorage, const std::vector<uint8_t>&, callerSessionId)
    ZCHATIM_JNI_FWD1(JniMapStringString, GetStorageStatus, const std::vector<uint8_t>&, callerSessionId)
    ZCHATIM_JNI_FWD1(int64_t, GetMessageCount, const std::vector<uint8_t>&, callerSessionId)
    ZCHATIM_JNI_FWD1(int64_t, GetFileCount, const std::vector<uint8_t>&, callerSessionId)

    ZCHATIM_JNI_FWD1(std::vector<uint8_t>, GenerateMasterKey, const std::vector<uint8_t>&, callerSessionId)
    ZCHATIM_JNI_FWD1(std::vector<uint8_t>, RefreshSessionKey, const std::vector<uint8_t>&, callerSessionId)
    ZCHATIM_JNI_FWD1(void, EmergencyWipe, const std::vector<uint8_t>&, callerSessionId)
    ZCHATIM_JNI_FWD1(JniMapStringString, GetStatus, const std::vector<uint8_t>&, callerSessionId)
    ZCHATIM_JNI_FWD1(bool, RotateKeys, const std::vector<uint8_t>&, callerSessionId)

    ZCHATIM_JNI_FWD3(void, ConfigurePinnedPublicKeyHashes, const std::vector<uint8_t>&, callerSessionId, const std::vector<uint8_t>&, currentSpkiSha256, const std::vector<uint8_t>&, standbySpkiSha256)
    ZCHATIM_JNI_FWD3(bool, VerifyPinnedServerCertificate, const std::vector<uint8_t>&, callerSessionId, const std::vector<uint8_t>&, clientId, const std::vector<uint8_t>&, presentedSpkiSha256)
    ZCHATIM_JNI_FWD2(bool, IsClientBanned, const std::vector<uint8_t>&, callerSessionId, const std::vector<uint8_t>&, clientId)
    ZCHATIM_JNI_FWD2(void, RecordFailure, const std::vector<uint8_t>&, callerSessionId, const std::vector<uint8_t>&, clientId)
    ZCHATIM_JNI_FWD2(void, ClearBan, const std::vector<uint8_t>&, callerSessionId, const std::vector<uint8_t>&, clientId)

    ZCHATIM_JNI_FWD4(bool, DeleteAccount, const std::vector<uint8_t>&, callerSessionId, const std::vector<uint8_t>&, userId, const std::vector<uint8_t>&, reauthToken, const std::vector<uint8_t>&, secondConfirmToken)
    ZCHATIM_JNI_FWD2(bool, IsAccountDeleted, const std::vector<uint8_t>&, callerSessionId, const std::vector<uint8_t>&, userId)
    ZCHATIM_JNI_FWD6(bool, UpdateFriendNote, const std::vector<uint8_t>&, callerSessionId, const std::vector<uint8_t>&, userId, const std::vector<uint8_t>&, friendId, const std::vector<uint8_t>&, newEncryptedNote, uint64_t, updateTimestampSeconds, const std::vector<uint8_t>&, signatureEd25519)

    ZCHATIM_JNI_FWD0(bool, ValidateJniCall)
    ZCHATIM_JNI_FWD2(bool, ValidateJniCall, const void*, jniEnv, const void*, jcls)

#undef ZCHATIM_JNI_FWD0
#undef ZCHATIM_JNI_FWD1
#undef ZCHATIM_JNI_FWD2
#undef ZCHATIM_JNI_FWD3
#undef ZCHATIM_JNI_FWD4
#undef ZCHATIM_JNI_FWD5
#undef ZCHATIM_JNI_FWD6
#undef ZCHATIM_JNI_FWD7
#undef ZCHATIM_JNI_FWD8

} // namespace ZChatIM::jni
