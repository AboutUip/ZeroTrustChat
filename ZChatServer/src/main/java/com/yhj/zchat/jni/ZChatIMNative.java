package com.yhj.zchat.jni;

import java.util.Map;

/**
 * ZChatIM C++ 桥：与 {@code jni/JniNatives.cpp} 中 {@code RegisterNatives} 一一对应。
 * 加载 {@code ZChatIMJNI}（CMake {@code OUTPUT_NAME}）。
 */
public final class ZChatIMNative {

    static {
        System.loadLibrary("ZChatIMJNI");
    }

    private ZChatIMNative() {}

    public static native boolean initialize(String dataDir, String indexDir);

    public static native void cleanup();

    public static native byte[] auth(byte[] userId, byte[] token, byte[] clientIp);

    public static native boolean verifySession(byte[] sessionId);

    public static native boolean destroySession(byte[] callerSessionId, byte[] sessionIdToDestroy);

    public static native byte[] storeMessage(byte[] callerSessionId, byte[] imSessionId, byte[] payload);

    public static native byte[] retrieveMessage(byte[] callerSessionId, byte[] messageId);

    public static native boolean deleteMessage(
            byte[] callerSessionId, byte[] messageId, byte[] senderId, byte[] signatureEd25519);

    public static native boolean recallMessage(
            byte[] callerSessionId, byte[] messageId, byte[] senderId, byte[] signatureEd25519);

    public static native byte[][] listMessages(byte[] callerSessionId, byte[] userId, int count);

    public static native byte[][] listMessagesSinceTimestamp(
            byte[] callerSessionId, byte[] userId, long sinceTimestampMs, int count);

    public static native byte[][] listMessagesSinceMessageId(
            byte[] callerSessionId, byte[] userId, byte[] lastMsgId, int count);

    public static native boolean markMessageRead(
            byte[] callerSessionId, byte[] messageId, long readTimestampMs);

    public static native byte[][] getUnreadSessionMessageIds(
            byte[] callerSessionId, byte[] imSessionId, int limit);

    public static native boolean storeMessageReplyRelation(
            byte[] callerSessionId,
            byte[] senderEd25519PublicKey,
            byte[] messageId,
            byte[] repliedMsgId,
            byte[] repliedSenderId,
            byte[] repliedContentDigest,
            byte[] senderId,
            byte[] signatureEd25519);

    public static native byte[][] getMessageReplyRelation(byte[] callerSessionId, byte[] messageId);

    public static native boolean editMessage(
            byte[] callerSessionId,
            byte[] messageId,
            byte[] newEncryptedContent,
            long editTimestampSeconds,
            byte[] signature,
            byte[] senderId);

    public static native byte[] getMessageEditState(byte[] callerSessionId, byte[] messageId);

    public static native boolean storeUserData(
            byte[] callerSessionId, byte[] userId, int type, byte[] data);

    public static native byte[] getUserData(byte[] callerSessionId, byte[] userId, int type);

    public static native boolean deleteUserData(byte[] callerSessionId, byte[] userId, int type);

    public static native byte[] sendFriendRequest(
            byte[] callerSessionId,
            byte[] fromUserId,
            byte[] toUserId,
            long timestampSeconds,
            byte[] signatureEd25519);

    public static native boolean respondFriendRequest(
            byte[] callerSessionId,
            byte[] requestId,
            boolean accept,
            byte[] responderId,
            long timestampSeconds,
            byte[] signatureEd25519);

    public static native boolean deleteFriend(
            byte[] callerSessionId,
            byte[] userId,
            byte[] friendId,
            long timestampSeconds,
            byte[] signatureEd25519);

    public static native byte[][] getFriends(byte[] callerSessionId, byte[] userId);

    public static native byte[] createGroup(byte[] callerSessionId, byte[] creatorId, String name);

    public static native boolean inviteMember(
            byte[] callerSessionId, byte[] groupId, byte[] userId);

    public static native boolean removeMember(
            byte[] callerSessionId, byte[] groupId, byte[] userId);

    public static native boolean leaveGroup(
            byte[] callerSessionId, byte[] groupId, byte[] userId);

    public static native byte[][] getGroupMembers(byte[] callerSessionId, byte[] groupId);

    public static native boolean updateGroupKey(byte[] callerSessionId, byte[] groupId);

    public static native boolean validateMentionRequest(
            byte[] callerSessionId,
            byte[] groupId,
            byte[] senderId,
            int mentionType,
            byte[][] mentionedUserIds,
            long nowMs,
            byte[] signatureEd25519);

    public static native boolean recordMentionAtAllUsage(
            byte[] callerSessionId, byte[] groupId, byte[] senderId, long nowMs);

    public static native boolean muteMember(
            byte[] callerSessionId,
            byte[] groupId,
            byte[] userId,
            byte[] mutedBy,
            long startTimeMs,
            long durationSeconds,
            byte[] reason);

    public static native boolean isMuted(
            byte[] callerSessionId, byte[] groupId, byte[] userId, long nowMs);

    public static native boolean unmuteMember(
            byte[] callerSessionId, byte[] groupId, byte[] userId, byte[] unmutedBy);

    public static native boolean updateGroupName(
            byte[] callerSessionId,
            byte[] groupId,
            byte[] updaterId,
            String newGroupName,
            long nowMs);

    public static native String getGroupName(byte[] callerSessionId, byte[] groupId);

    public static native boolean storeFileChunk(
            byte[] callerSessionId, String fileId, int chunkIndex, byte[] data);

    public static native byte[] getFileChunk(byte[] callerSessionId, String fileId, int chunkIndex);

    public static native boolean completeFile(
            byte[] callerSessionId, String fileId, byte[] sha256);

    public static native boolean cancelFile(byte[] callerSessionId, String fileId);

    public static native boolean storeTransferResumeChunkIndex(
            byte[] callerSessionId, String fileId, int chunkIndex);

    /** 失败时返回 -1（对应 C++ {@code UINT32_MAX} 约定）。 */
    public static native int getTransferResumeChunkIndex(byte[] callerSessionId, String fileId);

    public static native boolean cleanupTransferResumeChunkIndex(
            byte[] callerSessionId, String fileId);

    public static native byte[][] getSessionMessages(
            byte[] callerSessionId, byte[] imSessionId, int limit);

    public static native boolean getSessionStatus(byte[] callerSessionId, byte[] imSessionId);

    public static native void touchSession(
            byte[] callerSessionId, byte[] imSessionId, long nowMs);

    public static native void cleanupExpiredSessions(byte[] callerSessionId, long nowMs);

    /**
     * @return {@code null} 失败；非 null 时长度 0 表示成功且无设备被踢；长度 16 为被踢出的会话 id。
     */
    public static native byte[] registerDeviceSession(
            byte[] callerSessionId,
            byte[] userId,
            byte[] deviceId,
            byte[] sessionId,
            long loginTimeMs,
            long lastActiveMs);

    public static native boolean updateLastActive(
            byte[] callerSessionId, byte[] userId, byte[] sessionId, long nowMs);

    public static native byte[][] getDeviceSessions(byte[] callerSessionId, byte[] userId);

    public static native void cleanupExpiredDeviceSessions(byte[] callerSessionId, long nowMs);

    public static native boolean getUserStatus(byte[] callerSessionId, byte[] userId);

    public static native boolean cleanupSessionMessages(
            byte[] callerSessionId, byte[] imSessionId);

    public static native boolean cleanupExpiredData(byte[] callerSessionId);

    public static native boolean optimizeStorage(byte[] callerSessionId);

    public static native Map<String, String> getStorageStatus(byte[] callerSessionId);

    public static native long getMessageCount(byte[] callerSessionId);

    public static native long getFileCount(byte[] callerSessionId);

    public static native byte[] generateMasterKey(byte[] callerSessionId);

    public static native byte[] refreshSessionKey(byte[] callerSessionId);

    public static native void emergencyWipe(byte[] callerSessionId);

    public static native Map<String, String> getStatus(byte[] callerSessionId);

    public static native boolean rotateKeys(byte[] callerSessionId);

    public static native void configurePinnedPublicKeyHashes(
            byte[] callerSessionId, byte[] currentSpkiSha256, byte[] standbySpkiSha256);

    public static native boolean verifyPinnedServerCertificate(
            byte[] callerSessionId, byte[] clientId, byte[] presentedSpkiSha256);

    public static native boolean isClientBanned(byte[] callerSessionId, byte[] clientId);

    public static native void recordFailure(byte[] callerSessionId, byte[] clientId);

    public static native void clearBan(byte[] callerSessionId, byte[] clientId);

    public static native boolean deleteAccount(
            byte[] callerSessionId,
            byte[] userId,
            byte[] reauthToken,
            byte[] secondConfirmToken);

    public static native boolean isAccountDeleted(byte[] callerSessionId, byte[] userId);

    public static native boolean updateFriendNote(
            byte[] callerSessionId,
            byte[] userId,
            byte[] friendId,
            byte[] newEncryptedNote,
            long updateTimestampSeconds,
            byte[] signatureEd25519);

    public static native boolean validateJniCall();

    public static native boolean validateJniCall(Class<?> clazz);
}
