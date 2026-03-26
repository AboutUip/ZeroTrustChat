package com.ztrust.zchat.im.zsp.server;

import com.ztrust.zchat.im.jni.ZChatIMNative;
import org.springframework.beans.factory.DisposableBean;

import java.util.Map;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.logging.Level;
import java.util.logging.Logger;

/**
 * JNI 门面：必选 Bean；构造时加载并初始化原生库（{@link ZChatIMNative#initialize}）。
 * 方法签名与 {@link ZChatIMNative} 保持一致，便于网关全量调用 MM1/MM2。
 */
public final class ZChatNativeOperations implements ZspNativeGateway, DisposableBean {

    private static final Logger LOG = Logger.getLogger(ZChatNativeOperations.class.getName());
    private final AtomicBoolean cleaned = new AtomicBoolean(false);

    public ZChatNativeOperations(ZspServerProperties props) {
        String data = props.getNative().getDataDir();
        String index = props.getNative().getIndexDir();
        if (data == null || index == null) {
            throw new IllegalStateException("zchat.zsp.native.data-dir and index-dir are required");
        }
        boolean ok = ZChatIMNative.initialize(data, index);
        if (!ok) {
            String pw = props.getNative().getPassphrase();
            if (pw != null && !pw.isBlank()) {
                ok = ZChatIMNative.initializeWithPassphrase(data, index, pw);
            }
        }
        if (!ok) {
            String nativeDetail = ZChatIMNative.lastInitializeError();
            if (nativeDetail != null && !nativeDetail.isEmpty()) {
                LOG.log(Level.SEVERE, "ZChatIMNative.initialize failed: {0}", nativeDetail);
            }
            String suffix =
                    " Check directories exist and are writable; if MM2/SQLCipher requires a passphrase set "
                            + "zchat.zsp.native.passphrase. On JDK 24+ add VM option --enable-native-access=ALL-UNNAMED "
                            + "(see docs/03-Business/01-SpringBoot.md).";
            String detailPart =
                    (nativeDetail == null || nativeDetail.isEmpty())
                            ? ""
                            : (" Native: " + nativeDetail + ".");
            throw new IllegalStateException(
                    "ZChatIMNative.initialize returned false (data-dir="
                            + data
                            + ", index-dir="
                            + index
                            + ")."
                            + detailPart
                            + suffix);
        }
    }

    public boolean initializeWithPassphrase(String dataDir, String indexDir, String passphrase) {
        return ZChatIMNative.initializeWithPassphrase(dataDir, indexDir, passphrase);
    }

    public void cleanup() {
        if (!cleaned.compareAndSet(false, true)) {
            return;
        }
        try {
            ZChatIMNative.cleanup();
        } catch (RuntimeException ex) {
            LOG.log(Level.WARNING, "ZChatIMNative.cleanup failed", ex);
        }
    }

    @Override
    public void destroy() {
        cleanup();
    }

    public byte[] auth(byte[] userId, byte[] token, byte[] clientIp) {
        return ZChatIMNative.auth(userId, token, clientIp);
    }

    public boolean verifySession(byte[] sessionId) {
        return ZChatIMNative.verifySession(sessionId);
    }

    public boolean destroySession(byte[] callerSessionId, byte[] sessionIdToDestroy) {
        return ZChatIMNative.destroySession(callerSessionId, sessionIdToDestroy);
    }

    public void destroyCallerSession(byte[] callerSessionId) {
        ZChatIMNative.destroySession(callerSessionId, callerSessionId);
    }

    public boolean registerLocalUser(byte[] userId, byte[] password, byte[] recoveryToken) {
        return ZChatIMNative.registerLocalUser(userId, password, recoveryToken);
    }

    public byte[] authWithLocalPassword(byte[] userId, byte[] password, byte[] clientIp) {
        return ZChatIMNative.authWithLocalPassword(userId, password, clientIp);
    }

    public boolean hasLocalPassword(byte[] userId) {
        return ZChatIMNative.hasLocalPassword(userId);
    }

    public boolean changeLocalPassword(
            byte[] userId, byte[] oldPassword, byte[] newPassword, byte[] newRecoveryToken) {
        return ZChatIMNative.changeLocalPassword(userId, oldPassword, newPassword, newRecoveryToken);
    }

    public boolean resetLocalPasswordWithRecovery(
            byte[] userId, byte[] recoveryToken, byte[] newPassword, byte[] newRecoveryToken) {
        return ZChatIMNative.resetLocalPasswordWithRecovery(userId, recoveryToken, newPassword, newRecoveryToken);
    }

    public byte[] rtcStartCall(byte[] callerSessionId, byte[] peerUserId, int callKind) {
        return ZChatIMNative.rtcStartCall(callerSessionId, peerUserId, callKind);
    }

    public boolean rtcAcceptCall(byte[] callerSessionId, byte[] callId) {
        return ZChatIMNative.rtcAcceptCall(callerSessionId, callId);
    }

    public boolean rtcRejectCall(byte[] callerSessionId, byte[] callId) {
        return ZChatIMNative.rtcRejectCall(callerSessionId, callId);
    }

    public boolean rtcEndCall(byte[] callerSessionId, byte[] callId) {
        return ZChatIMNative.rtcEndCall(callerSessionId, callId);
    }

    public int rtcGetCallState(byte[] callerSessionId, byte[] callId) {
        return ZChatIMNative.rtcGetCallState(callerSessionId, callId);
    }

    public int rtcGetCallKind(byte[] callerSessionId, byte[] callId) {
        return ZChatIMNative.rtcGetCallKind(callerSessionId, callId);
    }

    public byte[] storeMessage(byte[] callerSessionId, byte[] imSessionId, byte[] payload) {
        return ZChatIMNative.storeMessage(callerSessionId, imSessionId, payload);
    }

    public byte[] retrieveMessage(byte[] callerSessionId, byte[] messageId) {
        return ZChatIMNative.retrieveMessage(callerSessionId, messageId);
    }

    public boolean deleteMessage(
            byte[] callerSessionId, byte[] messageId, byte[] senderId, byte[] signatureEd25519) {
        return ZChatIMNative.deleteMessage(callerSessionId, messageId, senderId, signatureEd25519);
    }

    public boolean recallMessage(
            byte[] callerSessionId, byte[] messageId, byte[] senderId, byte[] signatureEd25519) {
        return ZChatIMNative.recallMessage(callerSessionId, messageId, senderId, signatureEd25519);
    }

    public byte[][] listMessages(byte[] callerSessionId, byte[] userId, int count) {
        return ZChatIMNative.listMessages(callerSessionId, userId, count);
    }

    public byte[][] listMessagesSinceTimestamp(
            byte[] callerSessionId, byte[] userId, long sinceTimestampMs, int count) {
        return ZChatIMNative.listMessagesSinceTimestamp(callerSessionId, userId, sinceTimestampMs, count);
    }

    public byte[][] listMessagesSinceMessageId(
            byte[] callerSessionId, byte[] userId, byte[] lastMsgId, int count) {
        return ZChatIMNative.listMessagesSinceMessageId(callerSessionId, userId, lastMsgId, count);
    }

    public boolean markMessageRead(byte[] callerSessionId, byte[] messageId, long readTimestampMs) {
        return ZChatIMNative.markMessageRead(callerSessionId, messageId, readTimestampMs);
    }

    public byte[][] getUnreadSessionMessageIds(byte[] callerSessionId, byte[] imSessionId, int limit) {
        return ZChatIMNative.getUnreadSessionMessageIds(callerSessionId, imSessionId, limit);
    }

    public boolean storeMessageReplyRelation(
            byte[] callerSessionId,
            byte[] senderEd25519PublicKey,
            byte[] messageId,
            byte[] repliedMsgId,
            byte[] repliedSenderId,
            byte[] repliedContentDigest,
            byte[] senderId,
            byte[] signatureEd25519) {
        return ZChatIMNative.storeMessageReplyRelation(
                callerSessionId,
                senderEd25519PublicKey,
                messageId,
                repliedMsgId,
                repliedSenderId,
                repliedContentDigest,
                senderId,
                signatureEd25519);
    }

    public byte[][] getMessageReplyRelation(byte[] callerSessionId, byte[] messageId) {
        return ZChatIMNative.getMessageReplyRelation(callerSessionId, messageId);
    }

    public boolean editMessage(
            byte[] callerSessionId,
            byte[] messageId,
            byte[] newEncryptedContent,
            long editTimestampSeconds,
            byte[] signature,
            byte[] senderId) {
        return ZChatIMNative.editMessage(
                callerSessionId, messageId, newEncryptedContent, editTimestampSeconds, signature, senderId);
    }

    public byte[] getMessageEditState(byte[] callerSessionId, byte[] messageId) {
        return ZChatIMNative.getMessageEditState(callerSessionId, messageId);
    }

    public boolean storeUserData(byte[] callerSessionId, byte[] userId, int type, byte[] data) {
        return ZChatIMNative.storeUserData(callerSessionId, userId, type, data);
    }

    public byte[] getUserData(byte[] callerSessionId, byte[] userId, int type) {
        return ZChatIMNative.getUserData(callerSessionId, userId, type);
    }

    public boolean deleteUserData(byte[] callerSessionId, byte[] userId, int type) {
        return ZChatIMNative.deleteUserData(callerSessionId, userId, type);
    }

    public byte[] sendFriendRequest(
            byte[] callerSessionId,
            byte[] fromUserId,
            byte[] toUserId,
            long timestampSeconds,
            byte[] signatureEd25519) {
        return ZChatIMNative.sendFriendRequest(callerSessionId, fromUserId, toUserId, timestampSeconds, signatureEd25519);
    }

    public boolean respondFriendRequest(
            byte[] callerSessionId,
            byte[] requestId,
            boolean accept,
            byte[] responderId,
            long timestampSeconds,
            byte[] signatureEd25519) {
        return ZChatIMNative.respondFriendRequest(
                callerSessionId, requestId, accept, responderId, timestampSeconds, signatureEd25519);
    }

    public boolean deleteFriend(
            byte[] callerSessionId,
            byte[] userId,
            byte[] friendId,
            long timestampSeconds,
            byte[] signatureEd25519) {
        return ZChatIMNative.deleteFriend(callerSessionId, userId, friendId, timestampSeconds, signatureEd25519);
    }

    public byte[][] getFriends(byte[] callerSessionId, byte[] userId) {
        return ZChatIMNative.getFriends(callerSessionId, userId);
    }

    public byte[] createGroup(byte[] callerSessionId, byte[] creatorId, String name) {
        return ZChatIMNative.createGroup(callerSessionId, creatorId, name);
    }

    public boolean inviteMember(byte[] callerSessionId, byte[] groupId, byte[] userId) {
        return ZChatIMNative.inviteMember(callerSessionId, groupId, userId);
    }

    public boolean removeMember(byte[] callerSessionId, byte[] groupId, byte[] userId) {
        return ZChatIMNative.removeMember(callerSessionId, groupId, userId);
    }

    public boolean leaveGroup(byte[] callerSessionId, byte[] groupId, byte[] userId) {
        return ZChatIMNative.leaveGroup(callerSessionId, groupId, userId);
    }

    public byte[][] getGroupMembers(byte[] callerSessionId, byte[] groupId) {
        return ZChatIMNative.getGroupMembers(callerSessionId, groupId);
    }

    public boolean updateGroupKey(byte[] callerSessionId, byte[] groupId) {
        return ZChatIMNative.updateGroupKey(callerSessionId, groupId);
    }

    public boolean validateMentionRequest(
            byte[] callerSessionId,
            byte[] groupId,
            byte[] senderId,
            int mentionType,
            byte[][] mentionedUserIds,
            long nowMs,
            byte[] signatureEd25519) {
        return ZChatIMNative.validateMentionRequest(
                callerSessionId, groupId, senderId, mentionType, mentionedUserIds, nowMs, signatureEd25519);
    }

    public boolean recordMentionAtAllUsage(byte[] callerSessionId, byte[] groupId, byte[] senderId, long nowMs) {
        return ZChatIMNative.recordMentionAtAllUsage(callerSessionId, groupId, senderId, nowMs);
    }

    public boolean muteMember(
            byte[] callerSessionId,
            byte[] groupId,
            byte[] userId,
            byte[] mutedBy,
            long startTimeMs,
            long durationSeconds,
            byte[] reason) {
        return ZChatIMNative.muteMember(
                callerSessionId, groupId, userId, mutedBy, startTimeMs, durationSeconds, reason);
    }

    public boolean isMuted(byte[] callerSessionId, byte[] groupId, byte[] userId, long nowMs) {
        return ZChatIMNative.isMuted(callerSessionId, groupId, userId, nowMs);
    }

    public boolean unmuteMember(byte[] callerSessionId, byte[] groupId, byte[] userId, byte[] unmutedBy) {
        return ZChatIMNative.unmuteMember(callerSessionId, groupId, userId, unmutedBy);
    }

    public boolean updateGroupName(
            byte[] callerSessionId, byte[] groupId, byte[] updaterId, String newGroupName, long nowMs) {
        return ZChatIMNative.updateGroupName(callerSessionId, groupId, updaterId, newGroupName, nowMs);
    }

    public String getGroupName(byte[] callerSessionId, byte[] groupId) {
        return ZChatIMNative.getGroupName(callerSessionId, groupId);
    }

    public boolean storeFileChunk(byte[] callerSessionId, String fileId, int chunkIndex, byte[] data) {
        return ZChatIMNative.storeFileChunk(callerSessionId, fileId, chunkIndex, data);
    }

    public byte[] getFileChunk(byte[] callerSessionId, String fileId, int chunkIndex) {
        return ZChatIMNative.getFileChunk(callerSessionId, fileId, chunkIndex);
    }

    public boolean completeFile(byte[] callerSessionId, String fileId, byte[] sha256) {
        return ZChatIMNative.completeFile(callerSessionId, fileId, sha256);
    }

    public boolean cancelFile(byte[] callerSessionId, String fileId) {
        return ZChatIMNative.cancelFile(callerSessionId, fileId);
    }

    public boolean storeTransferResumeChunkIndex(byte[] callerSessionId, String fileId, int chunkIndex) {
        return ZChatIMNative.storeTransferResumeChunkIndex(callerSessionId, fileId, chunkIndex);
    }

    public int getTransferResumeChunkIndex(byte[] callerSessionId, String fileId) {
        return ZChatIMNative.getTransferResumeChunkIndex(callerSessionId, fileId);
    }

    public boolean cleanupTransferResumeChunkIndex(byte[] callerSessionId, String fileId) {
        return ZChatIMNative.cleanupTransferResumeChunkIndex(callerSessionId, fileId);
    }

    public byte[][] getSessionMessages(byte[] callerSessionId, byte[] imSessionId, int limit) {
        return ZChatIMNative.getSessionMessages(callerSessionId, imSessionId, limit);
    }

    public boolean getSessionStatus(byte[] callerSessionId, byte[] imSessionId) {
        return ZChatIMNative.getSessionStatus(callerSessionId, imSessionId);
    }

    public void touchSession(byte[] callerSessionId, byte[] imSessionId, long nowMs) {
        ZChatIMNative.touchSession(callerSessionId, imSessionId, nowMs);
    }

    public void cleanupExpiredSessions(byte[] callerSessionId, long nowMs) {
        ZChatIMNative.cleanupExpiredSessions(callerSessionId, nowMs);
    }

    public byte[] registerDeviceSession(
            byte[] callerSessionId,
            byte[] userId,
            byte[] deviceId,
            byte[] sessionId,
            long loginTimeMs,
            long lastActiveMs) {
        return ZChatIMNative.registerDeviceSession(
                callerSessionId, userId, deviceId, sessionId, loginTimeMs, lastActiveMs);
    }

    public boolean updateLastActive(byte[] callerSessionId, byte[] userId, byte[] sessionId, long nowMs) {
        return ZChatIMNative.updateLastActive(callerSessionId, userId, sessionId, nowMs);
    }

    public byte[][] getDeviceSessions(byte[] callerSessionId, byte[] userId) {
        return ZChatIMNative.getDeviceSessions(callerSessionId, userId);
    }

    public void cleanupExpiredDeviceSessions(byte[] callerSessionId, long nowMs) {
        ZChatIMNative.cleanupExpiredDeviceSessions(callerSessionId, nowMs);
    }

    public boolean getUserStatus(byte[] callerSessionId, byte[] userId) {
        return ZChatIMNative.getUserStatus(callerSessionId, userId);
    }

    public boolean cleanupSessionMessages(byte[] callerSessionId, byte[] imSessionId) {
        return ZChatIMNative.cleanupSessionMessages(callerSessionId, imSessionId);
    }

    public boolean cleanupExpiredData(byte[] callerSessionId) {
        return ZChatIMNative.cleanupExpiredData(callerSessionId);
    }

    public boolean optimizeStorage(byte[] callerSessionId) {
        return ZChatIMNative.optimizeStorage(callerSessionId);
    }

    public Map<String, String> getStorageStatus(byte[] callerSessionId) {
        return ZChatIMNative.getStorageStatus(callerSessionId);
    }

    public long getMessageCount(byte[] callerSessionId) {
        return ZChatIMNative.getMessageCount(callerSessionId);
    }

    public long getFileCount(byte[] callerSessionId) {
        return ZChatIMNative.getFileCount(callerSessionId);
    }

    public byte[] generateMasterKey(byte[] callerSessionId) {
        return ZChatIMNative.generateMasterKey(callerSessionId);
    }

    public byte[] refreshSessionKey(byte[] callerSessionId) {
        return ZChatIMNative.refreshSessionKey(callerSessionId);
    }

    public void emergencyWipe(byte[] callerSessionId) {
        ZChatIMNative.emergencyWipe(callerSessionId);
    }

    public Map<String, String> getStatus(byte[] callerSessionId) {
        return ZChatIMNative.getStatus(callerSessionId);
    }

    public boolean rotateKeys(byte[] callerSessionId) {
        return ZChatIMNative.rotateKeys(callerSessionId);
    }

    public void configurePinnedPublicKeyHashes(
            byte[] callerSessionId, byte[] currentSpkiSha256, byte[] standbySpkiSha256) {
        ZChatIMNative.configurePinnedPublicKeyHashes(callerSessionId, currentSpkiSha256, standbySpkiSha256);
    }

    public boolean verifyPinnedServerCertificate(
            byte[] callerSessionId, byte[] clientId, byte[] presentedSpkiSha256) {
        return ZChatIMNative.verifyPinnedServerCertificate(callerSessionId, clientId, presentedSpkiSha256);
    }

    public boolean isClientBanned(byte[] callerSessionId, byte[] clientId) {
        return ZChatIMNative.isClientBanned(callerSessionId, clientId);
    }

    public void recordFailure(byte[] callerSessionId, byte[] clientId) {
        ZChatIMNative.recordFailure(callerSessionId, clientId);
    }

    public void clearBan(byte[] callerSessionId, byte[] clientId) {
        ZChatIMNative.clearBan(callerSessionId, clientId);
    }

    public boolean deleteAccount(byte[] callerSessionId, byte[] userId, byte[] reauthToken, byte[] secondConfirmToken) {
        return ZChatIMNative.deleteAccount(callerSessionId, userId, reauthToken, secondConfirmToken);
    }

    public boolean isAccountDeleted(byte[] callerSessionId, byte[] userId) {
        return ZChatIMNative.isAccountDeleted(callerSessionId, userId);
    }

    public boolean updateFriendNote(
            byte[] callerSessionId,
            byte[] userId,
            byte[] friendId,
            byte[] newEncryptedNote,
            long updateTimestampSeconds,
            byte[] signatureEd25519) {
        return ZChatIMNative.updateFriendNote(
                callerSessionId, userId, friendId, newEncryptedNote, updateTimestampSeconds, signatureEd25519);
    }

    public boolean validateJniCall() {
        return ZChatIMNative.validateJniCall();
    }

    public boolean validateJniCall(Class<?> clazz) {
        return ZChatIMNative.validateJniCall(clazz);
    }
}
