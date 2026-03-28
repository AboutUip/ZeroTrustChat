package com.kite.zchat.zsp;

import com.kite.zchat.core.ServerEndpoint;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;

import androidx.annotation.Nullable;

/**
 * 保持单条 TCP 与 ZSP 递增序列号，在 {@link ZspProtocolConstants#LOCAL_PASSWORD_AUTH} 后发送需认证帧（头像/昵称等）。
 */
public final class ZspSessionManager {

    private static final int CONNECT_TIMEOUT_MS = 12_000;
    private static final int READ_TIMEOUT_MS = 60_000;

    private static final ZspSessionManager INSTANCE = new ZspSessionManager();

    private final Object lock = new Object();

    private Socket socket;
    private InputStream in;
    private OutputStream out;
    private long nextSequence = 1L;
    private ServerEndpoint endpoint;
    private byte[] userId16;

    /** 服务端推送的 TEXT 帧（与请求应答共用连接）；在读到非预期类型时先分发再续读。 */
    public interface IncomingTextListener {
        void onIncomingTextPayload(byte[] fullPayload);
    }

    @Nullable private volatile IncomingTextListener incomingTextListener;

    private ZspSessionManager() {}

    public static ZspSessionManager get() {
        return INSTANCE;
    }

    public void setIncomingTextListener(@Nullable IncomingTextListener listener) {
        incomingTextListener = listener;
    }

    /** 关闭连接（登出或切换账号前可调用）。 */
    public void close() {
        synchronized (lock) {
            closeLocked();
        }
    }

    public boolean isConnected() {
        synchronized (lock) {
            return socket != null && socket.isConnected() && in != null && out != null;
        }
    }

    /** 当前会话的 16 字节 userId；未连接时为 null。 */
    public byte[] getSessionUserId16() {
        synchronized (lock) {
            return userId16 != null ? Arrays.copyOf(userId16, userId16.length) : null;
        }
    }

    /**
     * 若已为同一 endpoint + userId 且仍连接，则复用；否则重新认证。
     */
    public boolean ensureSession(ServerEndpoint ep, byte[] uid16, String passwordUtf8) {
        if (ep == null || uid16 == null || uid16.length != ZspProtocolConstants.USER_ID_SIZE || passwordUtf8 == null) {
            return false;
        }
        synchronized (lock) {
            if (socket != null
                    && socket.isConnected()
                    && endpoint != null
                    && endpoint.equals(ep)
                    && userId16 != null
                    && Arrays.equals(userId16, uid16)) {
                return true;
            }
            return establishSessionLocked(ep, uid16, passwordUtf8);
        }
    }

    public boolean establishSession(ServerEndpoint ep, byte[] uid16, String passwordUtf8) {
        if (ep == null || uid16 == null || uid16.length != ZspProtocolConstants.USER_ID_SIZE || passwordUtf8 == null) {
            return false;
        }
        synchronized (lock) {
            return establishSessionLocked(ep, uid16, passwordUtf8);
        }
    }

    private boolean establishSessionLocked(ServerEndpoint ep, byte[] uid16, String passwordUtf8) {
        closeLocked();
        try {
            Socket s = new Socket();
            s.connect(new InetSocketAddress(ep.host(), ep.port()), CONNECT_TIMEOUT_MS);
            s.setSoTimeout(READ_TIMEOUT_MS);
            socket = s;
            in = s.getInputStream();
            out = s.getOutputStream();
            nextSequence = 1L;
            byte[] pw = passwordUtf8.getBytes(StandardCharsets.UTF_8);
            byte[] payload = ZspLocalAuthClient.buildPasswordAuthPayload(uid16, pw);
            byte[] frame =
                    ZspFrameCodec.encodeFrame(
                            ZspProtocolConstants.LOCAL_PASSWORD_AUTH, 0, 0L, nextSequence++, payload);
            out.write(frame);
            out.flush();
            ZspFrameCodec.ParsedFrame parsed = ZspFrameCodec.readFrame(in);
            if (parsed.messageType != ZspProtocolConstants.LOCAL_PASSWORD_AUTH) {
                closeLocked();
                return false;
            }
            if (parsed.payload == null || parsed.payload.length == 0) {
                closeLocked();
                return false;
            }
            endpoint = ep;
            userId16 = Arrays.copyOf(uid16, uid16.length);
            return true;
        } catch (IOException e) {
            closeLocked();
            return false;
        }
    }

    private void closeLocked() {
        try {
            if (socket != null) {
                socket.close();
            }
        } catch (IOException ignored) {
        }
        socket = null;
        in = null;
        out = null;
        endpoint = null;
        userId16 = null;
        nextSequence = 1L;
    }

    /**
     * 发送已认证请求并读取同类型应答载荷；若先读到服务端转发的 TEXT，则经 {@link #setIncomingTextListener}
     * 分发并继续读，直到类型匹配。
     */
    public byte[] request(int messageType, byte[] payload) {
        if (payload == null) {
            throw new IllegalArgumentException("payload");
        }
        synchronized (lock) {
            if (socket == null || !socket.isConnected() || out == null || in == null) {
                return null;
            }
            try {
                byte[] frame =
                        ZspFrameCodec.encodeFrame(messageType, 0, 0L, nextSequence++, payload);
                out.write(frame);
                out.flush();
                while (true) {
                    ZspFrameCodec.ParsedFrame parsed = ZspFrameCodec.readFrame(in);
                    if (parsed.messageType == messageType) {
                        return parsed.payload != null ? parsed.payload : new byte[0];
                    }
                    if (parsed.messageType == ZspProtocolConstants.MESSAGE_TEXT
                            && parsed.payload != null
                            && parsed.payload.length >= 32) {
                        IncomingTextListener l = incomingTextListener;
                        if (l != null) {
                            l.onIncomingTextPayload(parsed.payload);
                        }
                        continue;
                    }
                    closeLocked();
                    return null;
                }
            } catch (IOException e) {
                closeLocked();
                return null;
            }
        }
    }

    /** 单聊明文：imSession‖toUser‖UTF-8；成功应答复为 16 字节 messageId。 */
    public ZspChatWire.TextSendResult sendTextMessage(byte[] imSession16, byte[] toUserId16, String textUtf8) {
        byte[] pl = ZspChatWire.buildTextPayload(imSession16, toUserId16, textUtf8);
        if (pl.length == 0) {
            return new ZspChatWire.TextSendResult(false, null);
        }
        byte[] r = request(ZspProtocolConstants.MESSAGE_TEXT, pl);
        if (r == null || r.length != 16) {
            return new ZspChatWire.TextSendResult(false, null);
        }
        return new ZspChatWire.TextSendResult(true, r);
    }

    /** SYNC：载荷见 {@link ZspChatWire#buildSyncPayloadInitial} / {@link ZspChatWire#buildSyncPayloadSince}。 */
    public byte[] syncSessionMessages(byte[] syncPayload) {
        if (syncPayload == null) {
            throw new IllegalArgumentException("syncPayload");
        }
        return request(ZspProtocolConstants.SYNC, syncPayload);
    }

    public boolean userAvatarSet(byte[] jpegOrPng) {
        if (jpegOrPng == null || jpegOrPng.length > ZspProtocolConstants.MM1_USER_AVATAR_MAX_BYTES) {
            return false;
        }
        byte[] r = request(ZspProtocolConstants.USER_AVATAR_SET, jpegOrPng);
        return r != null && r.length == 1 && r[0] == 1;
    }

    public boolean userAvatarDelete() {
        byte[] r = request(ZspProtocolConstants.USER_AVATAR_DELETE, new byte[0]);
        return r != null && r.length == 1 && r[0] == 1;
    }

    /**
     * 读取目标用户头像原始字节（须为互为好友或本人）；与资料包中的头像同源，用于资料解析异常时的回退。
     */
    public byte[] userAvatarGet(byte[] targetUserId16) {
        if (targetUserId16 == null || targetUserId16.length != ZspProtocolConstants.USER_ID_SIZE) {
            return null;
        }
        byte[] raw = request(ZspProtocolConstants.USER_AVATAR_GET, targetUserId16);
        if (raw == null || raw.length == 0) {
            return null;
        }
        return Arrays.copyOf(raw, raw.length);
    }

    public boolean userDisplayNameSet(String utf8) {
        if (utf8 == null) {
            return false;
        }
        byte[] raw = utf8.getBytes(StandardCharsets.UTF_8);
        if (raw.length > ZspProtocolConstants.MM1_USER_DISPLAY_NAME_MAX_BYTES) {
            return false;
        }
        byte[] r = request(ZspProtocolConstants.USER_DISPLAY_NAME_SET, raw);
        return r != null && r.length == 1 && r[0] == 1;
    }

    /**
     * 服务端 JNI {@code deleteAccount}：载荷为口令 UTF-8 的 SHA-256（32 字节），作为双确认令牌各一份。
     */
    public boolean accountDelete(byte[] passwordSha256_32) {
        if (passwordSha256_32 == null || passwordSha256_32.length != 32) {
            return false;
        }
        byte[] r = request(ZspProtocolConstants.ACCOUNT_DELETE, passwordSha256_32);
        return r != null && r.length == 1 && r[0] == 1;
    }

    public ZspProfileCodec.UserProfile userProfileGet(byte[] targetUserId16) {
        if (targetUserId16 == null || targetUserId16.length != ZspProtocolConstants.USER_ID_SIZE) {
            return new ZspProfileCodec.UserProfile("", null);
        }
        byte[] raw = request(ZspProtocolConstants.USER_PROFILE_GET, targetUserId16);
        if (raw == null) {
            return new ZspProfileCodec.UserProfile("", null);
        }
        return ZspProfileCodec.parseProfilePayload(raw);
    }

    /**
     * 好友资料（与 {@link #userProfileGet} 应答格式相同，ZSP 类型为 {@link ZspProtocolConstants#FRIEND_INFO_GET}）。
     * 通讯录好友行应使用本方法，与协议语义一致。
     */
    public ZspProfileCodec.UserProfile friendInfoGet(byte[] friendUserId16) {
        if (friendUserId16 == null || friendUserId16.length != ZspProtocolConstants.USER_ID_SIZE) {
            return new ZspProfileCodec.UserProfile("", null);
        }
        byte[] raw = request(ZspProtocolConstants.FRIEND_INFO_GET, friendUserId16);
        if (raw == null) {
            return new ZspProfileCodec.UserProfile("", null);
        }
        return ZspProfileCodec.parseProfilePayload(raw);
    }

    /** 当前用户的好友 userId 列表（每项 16B）。 */
    public byte[][] friendListGet() {
        byte[] raw = request(ZspProtocolConstants.FRIEND_LIST_GET, new byte[0]);
        if (raw == null) {
            return new byte[0][];
        }
        return ZspContactsCodec.parseIdList16(raw);
    }

    /** 当前用户所在群 groupId 列表（每项 16B）；服务端未实现枚举时可能为空。 */
    public byte[][] groupListGet() {
        byte[] raw = request(ZspProtocolConstants.GROUP_LIST_GET, new byte[0]);
        if (raw == null) {
            return new byte[0][];
        }
        return ZspContactsCodec.parseIdList16(raw);
    }

    /** 群名与成员人数（须为群成员）。 */
    public ZspContactsCodec.GroupInfo groupInfoGet(byte[] groupId16) {
        if (groupId16 == null || groupId16.length != ZspProtocolConstants.USER_ID_SIZE) {
            return new ZspContactsCodec.GroupInfo("", 0);
        }
        byte[] raw = request(ZspProtocolConstants.GROUP_INFO_GET, groupId16);
        if (raw == null) {
            return new ZspContactsCodec.GroupInfo("", 0);
        }
        return ZspContactsCodec.parseGroupInfo(raw);
    }

    /** 将本机 Ed25519 公钥写入服务端 UserData（EDJ1）。 */
    public boolean identityEd25519Publish(byte[] publicKey32) {
        if (publicKey32 == null || publicKey32.length != ZspProtocolConstants.ED25519_PUBLIC_KEY_SIZE) {
            return false;
        }
        byte[] r = request(ZspProtocolConstants.IDENTITY_ED25519_PUBLISH, publicKey32);
        return r != null && r.length == 1 && r[0] == 1;
    }

    public byte[] friendRequestSend(byte[] wirePayload104) {
        if (wirePayload104 == null || wirePayload104.length != 16 + 16 + 8 + 64) {
            return null;
        }
        return request(ZspProtocolConstants.FRIEND_REQUEST, wirePayload104);
    }

    public boolean friendRequestRespond(byte[] wirePayload105) {
        if (wirePayload105 == null || wirePayload105.length != 16 + 1 + 16 + 8 + 64) {
            return false;
        }
        byte[] r = request(ZspProtocolConstants.FRIEND_RESPONSE, wirePayload105);
        return r != null && r.length == 1 && r[0] == 1;
    }

    /** 删除好友：载荷为 {@link com.kite.zchat.friends.FriendSigning#buildDeleteFriendWire}（104 字节）。 */
    public boolean friendDelete(byte[] wirePayload104) {
        if (wirePayload104 == null || wirePayload104.length != 16 + 16 + 8 + 64) {
            return false;
        }
        byte[] r = request(ZspProtocolConstants.DELETE_FRIEND, wirePayload104);
        return r != null && r.length == 1 && r[0] == 1;
    }

    public byte[] friendPendingListGet() {
        return request(ZspProtocolConstants.FRIEND_PENDING_LIST_GET, new byte[0]);
    }
}
