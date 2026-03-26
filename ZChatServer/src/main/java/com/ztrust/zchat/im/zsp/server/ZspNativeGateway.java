package com.ztrust.zchat.im.zsp.server;

/**
 * JNI 网关能力子集：仅包含 {@link ZspMessageDispatcher} 所需方法，便于测试桩与 {@link ZChatNativeOperations} 共用。
 */
public interface ZspNativeGateway {

    byte[] auth(byte[] userId, byte[] token, byte[] clientIp);

    boolean verifySession(byte[] sessionId);

    void destroyCallerSession(byte[] callerSessionId);

    byte[] storeMessage(byte[] callerSessionId, byte[] imSessionId, byte[] payload);

    boolean markMessageRead(byte[] callerSessionId, byte[] messageId, long readTimestampMs);

    byte[][] getSessionMessages(byte[] callerSessionId, byte[] imSessionId, int limit);

    byte[][] listMessagesSinceMessageId(byte[] callerSessionId, byte[] userId, byte[] lastMsgId, int count);

    boolean storeFileChunk(byte[] callerSessionId, String fileId, int chunkIndex, byte[] data);

    boolean completeFile(byte[] callerSessionId, String fileId, byte[] sha256);

    boolean storeTransferResumeChunkIndex(byte[] callerSessionId, String fileId, int chunkIndex);

    boolean cancelFile(byte[] callerSessionId, String fileId);

    byte[] rtcStartCall(byte[] callerSessionId, byte[] peerUserId, int callKind);

    boolean rtcAcceptCall(byte[] callerSessionId, byte[] callId);

    boolean rtcRejectCall(byte[] callerSessionId, byte[] callId);

    boolean rtcEndCall(byte[] callerSessionId, byte[] callId);

    byte[] sendFriendRequest(
            byte[] callerSessionId,
            byte[] fromUserId,
            byte[] toUserId,
            long timestampSeconds,
            byte[] signatureEd25519);

    boolean respondFriendRequest(
            byte[] callerSessionId,
            byte[] requestId,
            boolean accept,
            byte[] responderId,
            long timestampSeconds,
            byte[] signatureEd25519);

    boolean deleteFriend(
            byte[] callerSessionId,
            byte[] userId,
            byte[] friendId,
            long timestampSeconds,
            byte[] signatureEd25519);

    byte[] createGroup(byte[] callerSessionId, byte[] creatorId, String name);

    boolean inviteMember(byte[] callerSessionId, byte[] groupId, byte[] userId);

    boolean removeMember(byte[] callerSessionId, byte[] groupId, byte[] userId);

    boolean leaveGroup(byte[] callerSessionId, byte[] groupId, byte[] userId);

    boolean updateGroupName(
            byte[] callerSessionId,
            byte[] groupId,
            byte[] updaterId,
            String newGroupName,
            long nowMs);
}
