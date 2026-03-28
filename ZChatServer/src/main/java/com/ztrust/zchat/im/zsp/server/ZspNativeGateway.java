package com.ztrust.zchat.im.zsp.server;

/**
 * JNI 网关能力子集：仅包含 {@link ZspMessageDispatcher} 所需方法，便于测试桩与 {@link ZChatNativeOperations} 共用。
 */
public interface ZspNativeGateway {

    byte[] auth(byte[] userId, byte[] token, byte[] clientIp);

    /** 本地口令开户（B1b），见 {@link com.ztrust.zchat.im.jni.ZChatIMNative#registerLocalUser}。 */
    boolean registerLocalUser(byte[] userId, byte[] password, byte[] recoveryToken);

    /** 本地口令登录（B1b），见 {@link com.ztrust.zchat.im.jni.ZChatIMNative#authWithLocalPassword}。 */
    byte[] authWithLocalPassword(byte[] userId, byte[] password, byte[] clientIp);

    boolean verifySession(byte[] sessionId);

    void destroyCallerSession(byte[] callerSessionId);

    byte[] storeMessage(byte[] callerSessionId, byte[] imSessionId, byte[] payload);

    boolean markMessageRead(byte[] callerSessionId, byte[] messageId, long readTimestampMs);

    byte[][] getSessionMessages(byte[] callerSessionId, byte[] imSessionId, int limit);

    byte[][] listMessagesSinceMessageId(byte[] callerSessionId, byte[] userId, byte[] lastMsgId, int count);

    boolean storeUserData(byte[] callerSessionId, byte[] userId, int type, byte[] data);

    byte[] getUserData(byte[] callerSessionId, byte[] userId, int type);

    boolean deleteUserData(byte[] callerSessionId, byte[] userId, int type);

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

    /** 本地账户注销墓碑（ACD1），见 {@link com.ztrust.zchat.im.jni.ZChatIMNative#deleteAccount}。 */
    boolean deleteAccount(
            byte[] callerSessionId, byte[] userId, byte[] reauthToken, byte[] secondConfirmToken);

    /** 已接受好友 userId 列表（每项 16B），见 {@link com.ztrust.zchat.im.jni.ZChatIMNative#getFriends}。 */
    byte[][] getFriends(byte[] callerSessionId, byte[] userId);

    /** 待处理好友申请行，见 {@link com.ztrust.zchat.im.jni.ZChatIMNative#listPendingFriendRequests}。 */
    byte[][] listPendingFriendRequests(byte[] callerSessionId, byte[] userId);

    /** 群显示名；无或无权时实现可返回 {@code null} 或空串。 */
    String getGroupName(byte[] callerSessionId, byte[] groupId);

    /** 群成员 userId 列表（viewer 须为群成员），见 {@link com.ztrust.zchat.im.jni.ZChatIMNative#getGroupMembers}。 */
    byte[][] getGroupMembers(byte[] callerSessionId, byte[] groupId);

    /**
     * 当前用户所加入的全部群 ID（每项 16B）。原生库尚未实现时返回零长度数组。
     */
    byte[][] listGroupIdsForUser(byte[] callerSessionId, byte[] userId);
}
