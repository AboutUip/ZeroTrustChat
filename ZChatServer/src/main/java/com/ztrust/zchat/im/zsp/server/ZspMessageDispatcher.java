package com.ztrust.zchat.im.zsp.server;

import com.ztrust.zchat.im.zsp.MessageTypes;
import com.ztrust.zchat.im.zsp.ZspAuthPayload;
import com.ztrust.zchat.im.zsp.ZspConstants;
import com.ztrust.zchat.im.zsp.ZspFlags;
import com.ztrust.zchat.im.zsp.ZspFrame;
import com.ztrust.zchat.im.zsp.ZspFrameValidator;
import com.ztrust.zchat.im.zsp.ZspFrameWireEncoder;
import com.ztrust.zchat.im.zsp.security.ZspFrameTagService;
import com.ztrust.zchat.im.zsp.meta.ZspMetaCodec;
import com.ztrust.zchat.im.zsp.meta.ZspMetaSection;
import com.ztrust.zchat.im.zsp.payload.ZspPayloadReaders;
import com.ztrust.zchat.im.zsp.replay.ZspPeerReplayState;
import com.ztrust.zchat.im.zsp.routing.ZspRoutingEnvelope;
import io.netty.channel.Channel;
import io.netty.channel.ChannelFuture;
import io.netty.channel.ChannelHandlerContext;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.stereotype.Component;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.Optional;
import java.util.logging.Level;
import java.util.logging.Logger;

/**
 * ZSP 全量消息分发：明文载荷按 02-ZSP-Protocol.md 第六节解析；密文载荷走路由前缀 + opaque JNI。
 */
@Component
public final class ZspMessageDispatcher {

    private static final Logger LOG = Logger.getLogger(ZspMessageDispatcher.class.getName());

    private static final int DEFAULT_SYNC_LIMIT = 32;

    private final ZspServerProperties properties;
    private final ZspConnectionRegistry registry;
    private final ZspNativeGateway nativeOps;
    private final ZspFrameTagService tagService;
    private final ZspOutboundFrameBuilder outbound;
    private final ZspOfflineMessageQueue offlineQueue;
    private final ZspMetrics metrics;

    public ZspMessageDispatcher(
            ZspServerProperties properties,
            ZspConnectionRegistry registry,
            ZspFrameTagService tagService,
            ZspOutboundFrameBuilder outbound,
            ZspOfflineMessageQueue offlineQueue,
            ZspNativeGateway nativeOps,
            @Autowired(required = false) ZspMetrics metrics) {
        this.properties = properties;
        this.registry = registry;
        this.tagService = tagService;
        this.outbound = outbound;
        this.offlineQueue = offlineQueue;
        this.nativeOps = nativeOps;
        this.metrics = metrics;
    }

    /** 在业务侧编码为 {@link io.netty.buffer.ByteBuf} 再写出，管道中不再传递 {@link ZspFrame}。 */
    private static ChannelFuture writeFrame(ChannelHandlerContext ctx, ZspFrame frame) {
        return ctx.writeAndFlush(ZspFrameWireEncoder.toByteBuf(ctx.alloc(), frame));
    }

    public void dispatch(ChannelHandlerContext ctx, ZspFrame frame) {
        try {
            try {
                ZspFrameValidator.validateOrThrow(frame);
            } catch (RuntimeException e) {
                closeDiag(ctx, Level.WARNING, "frame_validation_failed");
                return;
            }

            if (properties.isRequireEncryptedFlag() && !ZspFlags.isEncrypted(frame.header().flags())) {
                closeDiag(ctx, Level.WARNING, "encrypted_flag_required");
                return;
            }

            if (!tagService.verifyInbound(frame)) {
                closeDiag(ctx, Level.WARNING, "inbound_auth_tag_mismatch");
                return;
            }

            if (properties.isRejectCompressedPayload() && ZspFlags.isCompressed(frame.header().flags())) {
                closeDiag(ctx, Level.WARNING, "compressed_rejected");
                return;
            }

            if (properties.isReplayProtectionEnabled()) {
                ZspMetaSection meta = ZspMetaCodec.parse(frame.meta());
                ZspPeerReplayState st =
                        ctx.channel().attr(ZspChannelAttributes.REPLAY_STATE).get();
                if (st == null) {
                    st = new ZspPeerReplayState();
                    ctx.channel().attr(ZspChannelAttributes.REPLAY_STATE).set(st);
                }
                long seq = frame.header().sequence();
                long now = System.currentTimeMillis();
                if (!st.accept(
                        seq,
                        meta.nonce12(),
                        meta.timestampMs(),
                        now,
                        properties.getReplayTimestampWindowMinutes())) {
                    closeDiag(ctx, Level.WARNING, "replay_or_timestamp_failed");
                    return;
                }
            }

            int type = frame.header().messageType();
            if (type == MessageTypes.HEARTBEAT) {
                handleHeartbeat(ctx, frame);
                return;
            }
            if (type == MessageTypes.LOCAL_REGISTER) {
                handleLocalRegister(ctx, frame);
                return;
            }
            if (type == MessageTypes.LOCAL_PASSWORD_AUTH) {
                handleLocalPasswordAuth(ctx, frame);
                return;
            }
            if (type == MessageTypes.AUTH) {
                handleAuth(ctx, frame);
                return;
            }
            if (type == MessageTypes.LOGOUT) {
                handleLogout(ctx);
                return;
            }

            byte[] caller = ctx.channel().attr(ZspChannelAttributes.CALLER_SESSION_ID).get();
            if (caller == null || caller.length == 0) {
                closeDiag(ctx, Level.WARNING, "unauthenticated");
                return;
            }

            if (!nativeOps.verifySession(caller)) {
                closeDiag(ctx, Level.WARNING, "invalid_caller_session");
                return;
            }

            byte[] payload = frame.payload();
            if (ZspFlags.isEncrypted(frame.header().flags())) {
                handleEncryptedOpaque(ctx, frame, caller, payload);
                return;
            }

            dispatchCleartext(ctx, frame, caller, type, payload);
        } catch (RuntimeException ex) {
            LOG.log(Level.WARNING, "zsp_dispatch_exception", ex);
            closeDiag(ctx, Level.WARNING, "dispatch_exception");
        }
    }

    private void handleHeartbeat(ChannelHandlerContext ctx, ZspFrame frame) {
        if (properties.isHeartbeatEcho()) {
            ZspFrame echo = outbound.replySameFlags(ctx, frame, MessageTypes.HEARTBEAT, new byte[0]);
            ChannelFuture f = writeFrame(ctx, echo);
            f.addListener(
                    cf -> {
                        if (!cf.isSuccess()) {
                            LOG.log(
                                    Level.WARNING,
                                    "[zsp] heartbeat_echo_write_failed peer=" + ctx.channel().remoteAddress(),
                                    cf.cause());
                            return;
                        }
                        if (properties.isLogInboundEvents()) {
                            LOG.info("[zsp] heartbeat_echo peer=" + ctx.channel().remoteAddress());
                        }
                    });
        }
    }

    private void handleLocalRegister(ChannelHandlerContext ctx, ZspFrame frame) {
        Optional<LocalRegisterPayload> parsed = parseLocalRegisterPayload(frame.payload());
        if (parsed.isEmpty()) {
            closeDiag(ctx, Level.WARNING, "invalid_local_register_payload");
            return;
        }
        LocalRegisterPayload p = parsed.get();
        boolean ok = nativeOps.registerLocalUser(p.userId(), p.password(), p.recovery());
        byte[] replyPayload = new byte[] {(byte) (ok ? 1 : 0)};
        ZspFrame reply = outbound.replySameFlags(ctx, frame, MessageTypes.LOCAL_REGISTER, replyPayload);
        writeFrame(ctx, reply);
    }

    private void handleLocalPasswordAuth(ChannelHandlerContext ctx, ZspFrame frame) {
        Optional<LocalPasswordPayload> parsed = parseLocalPasswordPayload(frame.payload());
        if (parsed.isEmpty()) {
            closeDiag(ctx, Level.WARNING, "invalid_local_password_auth_payload");
            return;
        }
        LocalPasswordPayload p = parsed.get();
        byte[] ip = remoteIpBytes(ctx);
        byte[] session = nativeOps.authWithLocalPassword(p.userId(), p.password(), ip);
        if (session == null || session.length == 0) {
            ZspFrame reply = outbound.replySameFlags(ctx, frame, MessageTypes.LOCAL_PASSWORD_AUTH, new byte[0]);
            writeFrame(ctx, reply);
            return;
        }
        if (session.length > ZspConstants.MAX_PAYLOAD_LENGTH_U16) {
            closeMetricOnly(ctx, "auth_session_oversize");
            return;
        }
        ctx.channel().attr(ZspChannelAttributes.CALLER_SESSION_ID).set(session);
        byte[] uid16 = copyUserId16(p.userId());
        ctx.channel().attr(ZspChannelAttributes.AUTH_USER_ID_16).set(uid16);
        registry.register(uid16, ctx.channel());

        ZspFrame reply = outbound.replySameFlags(ctx, frame, MessageTypes.LOCAL_PASSWORD_AUTH, session);
        writeFrame(ctx, reply);

        if (uid16 != null && uid16.length == ZspConstants.USER_ID_SIZE) {
            offlineQueue.drainTo(ctx, uid16, outbound);
        }
    }

    private void handleAuth(ChannelHandlerContext ctx, ZspFrame frame) {
        Optional<ZspAuthPayload> parsed = ZspAuthPayload.parse(frame.payload());
        if (parsed.isEmpty()) {
            closeDiag(ctx, Level.WARNING, "invalid_auth_payload");
            return;
        }
        ZspAuthPayload p = parsed.get();
        byte[] ip = remoteIpBytes(ctx);
        byte[] session = nativeOps.auth(p.userId(), p.token(), ip);
        if (session == null || session.length == 0) {
            closeDiag(ctx, Level.INFO, "auth_rejected");
            return;
        }
        if (session.length > ZspConstants.MAX_PAYLOAD_LENGTH_U16) {
            closeMetricOnly(ctx, "auth_session_oversize");
            return;
        }
        ctx.channel().attr(ZspChannelAttributes.CALLER_SESSION_ID).set(session);
        byte[] uid16 = copyUserId16(p.userId());
        ctx.channel().attr(ZspChannelAttributes.AUTH_USER_ID_16).set(uid16);
        registry.register(uid16, ctx.channel());

        ZspFrame reply = outbound.replySameFlags(ctx, frame, MessageTypes.AUTH, session);
        writeFrame(ctx, reply);

        if (uid16 != null && uid16.length == ZspConstants.USER_ID_SIZE) {
            offlineQueue.drainTo(ctx, uid16, outbound);
        }
    }

    private static byte[] copyUserId16(byte[] userId) {
        if (userId == null || userId.length != ZspConstants.USER_ID_SIZE) {
            return userId;
        }
        return Arrays.copyOf(userId, userId.length);
    }

    private void handleLogout(ChannelHandlerContext ctx) {
        byte[] sid = ctx.channel().attr(ZspChannelAttributes.CALLER_SESSION_ID).get();
        byte[] uid = ctx.channel().attr(ZspChannelAttributes.AUTH_USER_ID_16).get();
        if (uid != null) {
            registry.unregister(uid);
        }
        if (sid != null) {
            nativeOps.destroyCallerSession(sid);
        }
        ctx.channel().attr(ZspChannelAttributes.CALLER_SESSION_ID).set(null);
        ctx.channel().attr(ZspChannelAttributes.AUTH_USER_ID_16).set(null);
        if (metrics != null) {
            metrics.recordLogout();
        }
        ctx.close();
    }

    private void handleEncryptedOpaque(ChannelHandlerContext ctx, ZspFrame frame, byte[] caller, byte[] payload) {
        Optional<ZspRoutingEnvelope.Split> split =
                ZspRoutingEnvelope.splitOpaque(payload, properties.getOpaqueRoutingMinBytes());
        if (split.isEmpty()) {
            closeDiag(ctx, Level.WARNING, "opaque_routing_too_short");
            return;
        }
        ZspRoutingEnvelope.Split s = split.get();
        byte[] body = s.body();
        byte[] to = s.toUserIdOrNull();
        byte[] msgId = nativeOps.storeMessage(caller, s.imSessionId(), body);
        if (msgId != null && msgId.length > 0 && to != null && !isAllZero(to)) {
            forwardFrame(ctx, frame, to);
        }
    }

    private void dispatchCleartext(ChannelHandlerContext ctx, ZspFrame frame, byte[] caller, int type, byte[] payload) {
        switch (type) {
            case MessageTypes.TEXT, MessageTypes.IMAGE, MessageTypes.VOICE, MessageTypes.VIDEO, MessageTypes.TYPING,
                    MessageTypes.CUSTOM -> cleartextImStore(ctx, frame, caller, payload);
            case MessageTypes.RECEIPT, MessageTypes.ACK -> cleartextReceiptAck(ctx, frame, caller, type, payload);
            case MessageTypes.SYNC -> cleartextSync(ctx, frame, caller, payload);
            case MessageTypes.FILE_INFO -> {
                nativeOps.storeMessage(caller, extractImSession(payload), payload);
            }
            case MessageTypes.FILE_CHUNK -> cleartextFileChunk(ctx, caller, payload);
            case MessageTypes.FILE_COMPLETE -> cleartextFileComplete(ctx, caller, payload);
            case MessageTypes.RESUME_TRANSFER -> cleartextResume(ctx, caller, payload);
            case MessageTypes.CANCEL_TRANSFER -> cleartextCancel(ctx, caller, payload);
            case MessageTypes.VOICE_CALL -> cleartextRtcStart(ctx, frame, caller, payload, 0);
            case MessageTypes.VIDEO_CALL -> cleartextRtcStart(ctx, frame, caller, payload, 1);
            case MessageTypes.CALL_SIGNAL -> cleartextCallSignal(ctx, caller, payload);
            case MessageTypes.FRIEND_REQUEST -> cleartextFriendRequest(ctx, frame, caller, payload);
            case MessageTypes.FRIEND_RESPONSE -> cleartextFriendResponse(ctx, frame, caller, payload);
            case MessageTypes.DELETE_FRIEND -> cleartextDeleteFriend(ctx, frame, caller, payload);
            case MessageTypes.FRIEND_NOTE_UPDATE -> cleartextFriendNote(ctx, frame, caller, payload);
            case MessageTypes.GROUP_CREATE -> cleartextGroupCreate(ctx, frame, caller, payload);
            case MessageTypes.GROUP_INVITE, MessageTypes.GROUP_REMOVE, MessageTypes.GROUP_LEAVE -> cleartextGroupPair(
                    ctx, frame, caller, type, payload);
            case MessageTypes.GROUP_UPDATE, MessageTypes.GROUP_MUTE, MessageTypes.GROUP_TRANSFER_OWNER,
                    MessageTypes.GROUP_JOIN_REQUEST -> {
                nativeOps.storeMessage(caller, extractImSession(payload), payload);
            }
            case MessageTypes.GROUP_NAME_UPDATE -> cleartextGroupNameUpdate(ctx, frame, caller, payload);
            case MessageTypes.USER_AVATAR_SET -> cleartextUserAvatarSet(ctx, frame, caller, payload);
            case MessageTypes.USER_AVATAR_GET -> cleartextUserAvatarGet(ctx, frame, caller, payload);
            case MessageTypes.USER_AVATAR_DELETE -> cleartextUserAvatarDelete(ctx, frame, caller);
            case MessageTypes.USER_DISPLAY_NAME_SET -> cleartextUserDisplayNameSet(ctx, frame, caller, payload);
            case MessageTypes.USER_DISPLAY_NAME_GET -> cleartextUserDisplayNameGet(ctx, frame, caller, payload);
            case MessageTypes.USER_PROFILE_GET -> cleartextUserProfileGet(ctx, frame, caller, payload);
            case MessageTypes.FRIEND_INFO_GET -> cleartextFriendInfoGet(ctx, frame, caller, payload);
            case MessageTypes.FRIEND_LIST_GET -> cleartextFriendListGet(ctx, frame, caller, payload);
            case MessageTypes.FRIEND_PENDING_LIST_GET -> cleartextFriendPendingListGet(ctx, frame, caller, payload);
            case MessageTypes.IDENTITY_ED25519_PUBLISH -> cleartextIdentityEd25519Publish(ctx, frame, caller, payload);
            case MessageTypes.GROUP_LIST_GET -> cleartextGroupListGet(ctx, frame, caller, payload);
            case MessageTypes.GROUP_INFO_GET -> cleartextGroupInfoGet(ctx, frame, caller, payload);
            case MessageTypes.ACCOUNT_DELETE -> cleartextAccountDelete(ctx, frame, caller, payload);
            default -> nativeOps.storeMessage(caller, extractImSession(payload), payload);
        }
    }

    private void cleartextUserAvatarSet(ChannelHandlerContext ctx, ZspFrame frame, byte[] caller, byte[] payload) {
        byte[] uid = ctx.channel().attr(ZspChannelAttributes.AUTH_USER_ID_16).get();
        if (uid == null || uid.length != ZspConstants.USER_ID_SIZE) {
            closeMetricOnly(ctx, "cleartext_invalid");
            return;
        }
        if (payload == null || payload.length > ZspConstants.MM1_USER_AVATAR_MAX_BYTES) {
            closeMetricOnly(ctx, "cleartext_invalid");
            return;
        }
        boolean ok =
                nativeOps.storeUserData(caller, uid, ZspConstants.MM1_USER_KV_TYPE_AVATAR_V1, payload);
        byte[] reply = new byte[] {(byte) (ok ? 1 : 0)};
        writeFrame(ctx, outbound.replySameFlags(ctx, frame, MessageTypes.USER_AVATAR_SET, reply));
    }

    private void cleartextUserAvatarGet(ChannelHandlerContext ctx, ZspFrame frame, byte[] caller, byte[] payload) {
        if (payload == null || payload.length != ZspConstants.USER_ID_SIZE) {
            closeMetricOnly(ctx, "cleartext_invalid");
            return;
        }
        byte[] target = Arrays.copyOf(payload, ZspConstants.USER_ID_SIZE);
        byte[] data = nativeOps.getUserData(caller, target, ZspConstants.MM1_USER_KV_TYPE_AVATAR_V1);
        if (data == null) {
            data = new byte[0];
        }
        writeFrame(ctx, outbound.replySameFlags(ctx, frame, MessageTypes.USER_AVATAR_GET, data));
    }

    private void cleartextUserAvatarDelete(ChannelHandlerContext ctx, ZspFrame frame, byte[] caller) {
        byte[] uid = ctx.channel().attr(ZspChannelAttributes.AUTH_USER_ID_16).get();
        if (uid == null || uid.length != ZspConstants.USER_ID_SIZE) {
            closeMetricOnly(ctx, "cleartext_invalid");
            return;
        }
        boolean ok = nativeOps.deleteUserData(caller, uid, ZspConstants.MM1_USER_KV_TYPE_AVATAR_V1);
        byte[] reply = new byte[] {(byte) (ok ? 1 : 0)};
        writeFrame(ctx, outbound.replySameFlags(ctx, frame, MessageTypes.USER_AVATAR_DELETE, reply));
    }

    private void cleartextUserDisplayNameSet(ChannelHandlerContext ctx, ZspFrame frame, byte[] caller, byte[] payload) {
        byte[] uid = ctx.channel().attr(ZspChannelAttributes.AUTH_USER_ID_16).get();
        if (uid == null || uid.length != ZspConstants.USER_ID_SIZE) {
            closeMetricOnly(ctx, "cleartext_invalid");
            return;
        }
        byte[] data = payload == null ? new byte[0] : payload;
        if (data.length > ZspConstants.MM1_USER_DISPLAY_NAME_MAX_BYTES) {
            closeMetricOnly(ctx, "cleartext_invalid");
            return;
        }
        boolean ok =
                nativeOps.storeUserData(caller, uid, ZspConstants.MM1_USER_KV_TYPE_DISPLAY_NAME_V1, data);
        byte[] reply = new byte[] {(byte) (ok ? 1 : 0)};
        writeFrame(ctx, outbound.replySameFlags(ctx, frame, MessageTypes.USER_DISPLAY_NAME_SET, reply));
    }

    private void cleartextUserDisplayNameGet(ChannelHandlerContext ctx, ZspFrame frame, byte[] caller, byte[] payload) {
        if (payload == null || payload.length != ZspConstants.USER_ID_SIZE) {
            closeMetricOnly(ctx, "cleartext_invalid");
            return;
        }
        byte[] target = Arrays.copyOf(payload, ZspConstants.USER_ID_SIZE);
        byte[] data = nativeOps.getUserData(caller, target, ZspConstants.MM1_USER_KV_TYPE_DISPLAY_NAME_V1);
        if (data == null) {
            data = new byte[0];
        }
        writeFrame(ctx, outbound.replySameFlags(ctx, frame, MessageTypes.USER_DISPLAY_NAME_GET, data));
    }

    private void cleartextUserProfileGet(ChannelHandlerContext ctx, ZspFrame frame, byte[] caller, byte[] payload) {
        Optional<byte[]> body = buildUserProfilePayload(caller, payload);
        if (body.isEmpty()) {
            closeMetricOnly(ctx, "cleartext_invalid");
            return;
        }
        writeFrame(ctx, outbound.replySameFlags(ctx, frame, MessageTypes.USER_PROFILE_GET, body.get()));
    }

    /** 与 {@link #cleartextUserProfileGet} 相同语义，应答类型为 {@link MessageTypes#FRIEND_INFO_GET}。 */
    private void cleartextFriendInfoGet(ChannelHandlerContext ctx, ZspFrame frame, byte[] caller, byte[] payload) {
        Optional<byte[]> body = buildUserProfilePayload(caller, payload);
        if (body.isEmpty()) {
            closeMetricOnly(ctx, "cleartext_invalid");
            return;
        }
        writeFrame(ctx, outbound.replySameFlags(ctx, frame, MessageTypes.FRIEND_INFO_GET, body.get()));
    }

    /**
     * 请求载荷须为 16B targetUserId；应答为 nickLen‖nick‖avatarLen‖avatar（与 USER_PROFILE_GET 一致）。
     */
    private Optional<byte[]> buildUserProfilePayload(byte[] caller, byte[] payload) {
        if (payload == null || payload.length != ZspConstants.USER_ID_SIZE) {
            return Optional.empty();
        }
        byte[] target = Arrays.copyOf(payload, ZspConstants.USER_ID_SIZE);
        byte[] nick = nativeOps.getUserData(caller, target, ZspConstants.MM1_USER_KV_TYPE_DISPLAY_NAME_V1);
        byte[] avatar = nativeOps.getUserData(caller, target, ZspConstants.MM1_USER_KV_TYPE_AVATAR_V1);
        if (nick == null) {
            nick = new byte[0];
        }
        if (avatar == null) {
            avatar = new byte[0];
        }
        if (nick.length > ZspConstants.MM1_USER_DISPLAY_NAME_MAX_BYTES) {
            nick = Arrays.copyOf(nick, ZspConstants.MM1_USER_DISPLAY_NAME_MAX_BYTES);
        }
        int maxAvatar = ZspConstants.MAX_PAYLOAD_LENGTH_U16 - 4 - nick.length;
        if (maxAvatar < 0) {
            nick = new byte[0];
            maxAvatar = ZspConstants.MAX_PAYLOAD_LENGTH_U16 - 4;
        }
        if (avatar.length > maxAvatar) {
            avatar = Arrays.copyOf(avatar, maxAvatar);
        }
        ByteBuffer buf = ByteBuffer.allocate(2 + nick.length + 2 + avatar.length).order(ByteOrder.BIG_ENDIAN);
        buf.putShort((short) nick.length);
        buf.put(nick);
        buf.putShort((short) avatar.length);
        buf.put(avatar);
        return Optional.of(buf.array());
    }

    /** 请求载荷为空；应答 count(u32 BE) ‖ count×friendUserId(16)。 */
    private void cleartextIdentityEd25519Publish(ChannelHandlerContext ctx, ZspFrame frame, byte[] caller, byte[] payload) {
        byte[] uid = ctx.channel().attr(ZspChannelAttributes.AUTH_USER_ID_16).get();
        if (uid == null || uid.length != ZspConstants.USER_ID_SIZE) {
            closeMetricOnly(ctx, "cleartext_invalid");
            return;
        }
        if (payload == null || payload.length != ZspConstants.ED25519_PUBLIC_KEY_SIZE) {
            closeMetricOnly(ctx, "cleartext_invalid");
            return;
        }
        boolean ok =
                nativeOps.storeUserData(caller, uid, ZspConstants.MM1_USER_KV_TYPE_ED25519_PUBKEY_V1, payload);
        byte[] reply = new byte[] {(byte) (ok ? 1 : 0)};
        writeFrame(ctx, outbound.replySameFlags(ctx, frame, MessageTypes.IDENTITY_ED25519_PUBLISH, reply));
    }

    /** 请求载荷为空；应答 count(u32 BE) ‖ count×(requestId(16)‖from(16)‖createdSec(u64 BE))。 */
    private void cleartextFriendPendingListGet(ChannelHandlerContext ctx, ZspFrame frame, byte[] caller, byte[] payload) {
        if (payload != null && payload.length > 0) {
            closeMetricOnly(ctx, "cleartext_invalid");
            return;
        }
        byte[] uid = ctx.channel().attr(ZspChannelAttributes.AUTH_USER_ID_16).get();
        if (uid == null || uid.length != ZspConstants.USER_ID_SIZE) {
            closeMetricOnly(ctx, "cleartext_invalid");
            return;
        }
        byte[][] rows = nativeOps.listPendingFriendRequests(caller, uid);
        if (rows == null) {
            rows = new byte[0][];
        }
        int rowBytes = ZspConstants.USER_ID_SIZE + ZspConstants.USER_ID_SIZE + 8;
        int n = 0;
        for (byte[] row : rows) {
            if (row != null && row.length == rowBytes) {
                n++;
            }
        }
        ByteBuffer buf = ByteBuffer.allocate(4 + n * rowBytes).order(ByteOrder.BIG_ENDIAN);
        buf.putInt(n);
        for (byte[] row : rows) {
            if (row != null && row.length == rowBytes) {
                buf.put(row);
            }
        }
        writeFrame(ctx, outbound.replySameFlags(ctx, frame, MessageTypes.FRIEND_PENDING_LIST_GET, buf.array()));
    }

    private void cleartextFriendListGet(ChannelHandlerContext ctx, ZspFrame frame, byte[] caller, byte[] payload) {
        if (payload != null && payload.length > 0) {
            closeMetricOnly(ctx, "cleartext_invalid");
            return;
        }
        byte[] uid = ctx.channel().attr(ZspChannelAttributes.AUTH_USER_ID_16).get();
        if (uid == null || uid.length != ZspConstants.USER_ID_SIZE) {
            closeMetricOnly(ctx, "cleartext_invalid");
            return;
        }
        byte[][] friends = nativeOps.getFriends(caller, uid);
        if (friends == null) {
            friends = new byte[0][];
        }
        int n = 0;
        for (byte[] f : friends) {
            if (f != null && f.length == ZspConstants.USER_ID_SIZE) {
                n++;
            }
        }
        ByteBuffer buf =
                ByteBuffer.allocate(4 + n * ZspConstants.USER_ID_SIZE).order(ByteOrder.BIG_ENDIAN);
        buf.putInt(n);
        for (byte[] f : friends) {
            if (f != null && f.length == ZspConstants.USER_ID_SIZE) {
                buf.put(f);
            }
        }
        writeFrame(ctx, outbound.replySameFlags(ctx, frame, MessageTypes.FRIEND_LIST_GET, buf.array()));
    }

    /** 请求载荷为空；应答 count(u32 BE) ‖ count×groupId(16)。底层未实现枚举时 count=0。 */
    private void cleartextGroupListGet(ChannelHandlerContext ctx, ZspFrame frame, byte[] caller, byte[] payload) {
        if (payload != null && payload.length > 0) {
            closeMetricOnly(ctx, "cleartext_invalid");
            return;
        }
        byte[] uid = ctx.channel().attr(ZspChannelAttributes.AUTH_USER_ID_16).get();
        if (uid == null || uid.length != ZspConstants.USER_ID_SIZE) {
            closeMetricOnly(ctx, "cleartext_invalid");
            return;
        }
        byte[][] gids = nativeOps.listGroupIdsForUser(caller, uid);
        if (gids == null) {
            gids = new byte[0][];
        }
        int n = 0;
        for (byte[] g : gids) {
            if (g != null && g.length == ZspConstants.USER_ID_SIZE) {
                n++;
            }
        }
        ByteBuffer buf =
                ByteBuffer.allocate(4 + n * ZspConstants.USER_ID_SIZE).order(ByteOrder.BIG_ENDIAN);
        buf.putInt(n);
        for (byte[] g : gids) {
            if (g != null && g.length == ZspConstants.USER_ID_SIZE) {
                buf.put(g);
            }
        }
        writeFrame(ctx, outbound.replySameFlags(ctx, frame, MessageTypes.GROUP_LIST_GET, buf.array()));
    }

    /** 请求 groupId(16)；应答 nameLen‖name UTF-8‖memberCount‖members×16B。 */
    private void cleartextGroupInfoGet(ChannelHandlerContext ctx, ZspFrame frame, byte[] caller, byte[] payload) {
        if (payload == null || payload.length != ZspConstants.USER_ID_SIZE) {
            closeMetricOnly(ctx, "cleartext_invalid");
            return;
        }
        byte[] gid = Arrays.copyOf(payload, ZspConstants.USER_ID_SIZE);
        String name = nativeOps.getGroupName(caller, gid);
        if (name == null) {
            name = "";
        }
        byte[] nameBytes = name.getBytes(StandardCharsets.UTF_8);
        if (nameBytes.length > 65535) {
            nameBytes = Arrays.copyOf(nameBytes, 65535);
        }
        byte[][] members = nativeOps.getGroupMembers(caller, gid);
        if (members == null) {
            members = new byte[0][];
        }
        int m = 0;
        for (byte[] u : members) {
            if (u != null && u.length == ZspConstants.USER_ID_SIZE) {
                m++;
            }
        }
        ByteBuffer buf =
                ByteBuffer.allocate(2 + nameBytes.length + 4 + m * ZspConstants.USER_ID_SIZE)
                        .order(ByteOrder.BIG_ENDIAN);
        buf.putShort((short) nameBytes.length);
        buf.put(nameBytes);
        buf.putInt(m);
        for (byte[] u : members) {
            if (u != null && u.length == ZspConstants.USER_ID_SIZE) {
                buf.put(u);
            }
        }
        writeFrame(ctx, outbound.replySameFlags(ctx, frame, MessageTypes.GROUP_INFO_GET, buf.array()));
    }

    private void cleartextAccountDelete(ChannelHandlerContext ctx, ZspFrame frame, byte[] caller, byte[] payload) {
        byte[] uid = ctx.channel().attr(ZspChannelAttributes.AUTH_USER_ID_16).get();
        if (uid == null || uid.length != ZspConstants.USER_ID_SIZE) {
            closeMetricOnly(ctx, "cleartext_invalid");
            return;
        }
        if (payload == null || payload.length != 32) {
            closeMetricOnly(ctx, "cleartext_invalid");
            return;
        }
        byte[] token = Arrays.copyOf(payload, 32);
        boolean ok = nativeOps.deleteAccount(caller, uid, token, token);
        byte[] reply = new byte[] {(byte) (ok ? 1 : 0)};
        ChannelFuture f = writeFrame(ctx, outbound.replySameFlags(ctx, frame, MessageTypes.ACCOUNT_DELETE, reply));
        if (ok) {
            f.addListener(future -> handleLogout(ctx));
        }
    }

    private void cleartextImStore(ChannelHandlerContext ctx, ZspFrame frame, byte[] caller, byte[] payload) {
        byte[] im = extractImSession(payload);
        byte[] body = payload.length > ZspConstants.USER_ID_SIZE
                ? Arrays.copyOfRange(payload, ZspConstants.USER_ID_SIZE, payload.length)
                : new byte[0];
        byte[] msgId = nativeOps.storeMessage(caller, im, body);
        if (msgId != null && msgId.length > 0) {
            maybeForwardCleartext(ctx, frame, payload, im);
            writeFrame(ctx, outbound.replySameFlags(ctx, frame, frame.header().messageType(), msgId));
        } else {
            writeFrame(ctx, outbound.replySameFlags(ctx, frame, frame.header().messageType(), new byte[0]));
        }
    }

    private void maybeForwardCleartext(ChannelHandlerContext ctx, ZspFrame frame, byte[] payload, byte[] imSession) {
        if (payload.length < ZspConstants.USER_ID_SIZE * 2) {
            return;
        }
        byte[] to = Arrays.copyOfRange(payload, ZspConstants.USER_ID_SIZE, ZspConstants.USER_ID_SIZE * 2);
        if (!isAllZero(to)) {
            forwardFrame(ctx, frame, to);
        }
    }

    private void cleartextReceiptAck(ChannelHandlerContext ctx, ZspFrame frame, byte[] caller, int type, byte[] p) {
        Optional<ZspPayloadReaders.MessageIdTs> m = ZspPayloadReaders.parseMessageIdAndOptionalTs(p);
        if (m.isEmpty()) {
            closeMetricOnly(ctx, "cleartext_invalid");
            return;
        }
        long ts = m.get().timestampMs();
        if (ts <= 0L) {
            ts = System.currentTimeMillis();
        }
        nativeOps.markMessageRead(caller, m.get().messageId16(), ts);
        if (type == MessageTypes.ACK) {
            ZspFrame ack = outbound.replySameFlags(ctx, frame, MessageTypes.ACK, new byte[0]);
            writeFrame(ctx, ack);
        }
    }

    /**
     * SYNC 载荷：① imSessionId(16)；② 扩展 A：imSessionId | lastMsgId(16) [| limit(4)]；
     * ③ 扩展 B：imSessionId | jniListUserId(16) | lastMsgId(16) [| limit(4)]（≥48 字节时启用 listUserId）。
     */
    private void cleartextSync(ChannelHandlerContext ctx, ZspFrame frame, byte[] caller, byte[] p) {
        if (p.length < ZspConstants.USER_ID_SIZE) {
            closeMetricOnly(ctx, "cleartext_invalid");
            return;
        }
        byte[] im = Arrays.copyOfRange(p, 0, ZspConstants.USER_ID_SIZE);
        int limit = DEFAULT_SYNC_LIMIT;
        byte[] last = null;
        byte[] listUserId = im;

        if (p.length >= 48) {
            listUserId = Arrays.copyOfRange(p, ZspConstants.USER_ID_SIZE, ZspConstants.USER_ID_SIZE * 2);
            last = Arrays.copyOfRange(p, ZspConstants.USER_ID_SIZE * 2, ZspConstants.USER_ID_SIZE * 3);
            if (p.length >= 52) {
                limit = ByteBuffer.wrap(p, 48, 4).order(ByteOrder.BIG_ENDIAN).getInt();
            }
        } else if (p.length >= ZspConstants.USER_ID_SIZE * 2) {
            last = Arrays.copyOfRange(p, ZspConstants.USER_ID_SIZE, ZspConstants.USER_ID_SIZE * 2);
            if (p.length >= ZspConstants.USER_ID_SIZE * 2 + 4) {
                limit = ByteBuffer.wrap(p, ZspConstants.USER_ID_SIZE * 2, 4).order(ByteOrder.BIG_ENDIAN).getInt();
            }
        }
        if (limit <= 0 || limit > 10_000) {
            limit = DEFAULT_SYNC_LIMIT;
        }

        byte[][] rows;
        if (last == null || isAllZero(last)) {
            rows = nativeOps.getSessionMessages(caller, im, limit);
        } else {
            rows = nativeOps.listMessagesSinceMessageId(caller, listUserId, last, limit);
        }
        byte[] packed = packSessionRows(rows);
        ZspFrame out = outbound.replySameFlags(ctx, frame, MessageTypes.SYNC, packed);
        writeFrame(ctx, out);
    }

    private static byte[] packSessionRows(byte[][] rows) {
        if (rows == null || rows.length == 0) {
            return new byte[4];
        }
        final int maxPayload = ZspConstants.MAX_PAYLOAD_LENGTH_U16;
        ByteBuffer buf = ByteBuffer.allocate(maxPayload).order(ByteOrder.BIG_ENDIAN);
        buf.putInt(0); // 先占位，最终回填实际条数
        int count = 0;
        for (byte[] r : rows) {
            int rowLen = r != null ? r.length : 0;
            if (rowLen > maxPayload - 8) {
                break;
            }
            if (buf.position() + 4 + rowLen > maxPayload) {
                break;
            }
            buf.putInt(rowLen);
            if (rowLen > 0) {
                buf.put(r);
            }
            count++;
        }
        buf.putInt(0, count);
        return Arrays.copyOf(buf.array(), buf.position());
    }

    private void cleartextFileChunk(ChannelHandlerContext ctx, byte[] caller, byte[] p) {
        Optional<ZspPayloadReaders.FileChunk> fc = ZspPayloadReaders.parseFileChunk(p);
        if (fc.isEmpty()) {
            closeMetricOnly(ctx, "cleartext_invalid");
            return;
        }
        ZspPayloadReaders.FileChunk c = fc.get();
        String fid = ZspPayloadReaders.fileIdBytesToString(c.fileId16());
        nativeOps.storeFileChunk(caller, fid, c.chunkIndex(), c.chunkData());
    }

    private void cleartextFileComplete(ChannelHandlerContext ctx, byte[] caller, byte[] p) {
        Optional<ZspPayloadReaders.FileComplete> fc = ZspPayloadReaders.parseFileComplete(p);
        if (fc.isEmpty()) {
            closeMetricOnly(ctx, "cleartext_invalid");
            return;
        }
        ZspPayloadReaders.FileComplete c = fc.get();
        String fid = ZspPayloadReaders.fileIdBytesToString(c.fileId16());
        nativeOps.completeFile(caller, fid, c.sha256());
    }

    private void cleartextResume(ChannelHandlerContext ctx, byte[] caller, byte[] p) {
        if (p.length < 2 + 4) {
            closeMetricOnly(ctx, "cleartext_invalid");
            return;
        }
        ByteBuffer buf = ByteBuffer.wrap(p).order(ByteOrder.BIG_ENDIAN);
        int slen = buf.getShort() & 0xFFFF;
        if (slen <= 0 || buf.remaining() < slen + 4) {
            closeMetricOnly(ctx, "cleartext_invalid");
            return;
        }
        byte[] raw = new byte[slen];
        buf.get(raw);
        String fileId = new String(raw, StandardCharsets.UTF_8);
        int cidx = buf.getInt();
        nativeOps.storeTransferResumeChunkIndex(caller, fileId, cidx);
    }

    private void cleartextCancel(ChannelHandlerContext ctx, byte[] caller, byte[] p) {
        if (p.length < 2) {
            closeMetricOnly(ctx, "cleartext_invalid");
            return;
        }
        int slen = ByteBuffer.wrap(p).order(ByteOrder.BIG_ENDIAN).getShort(0) & 0xFFFF;
        if (p.length < 2 + slen) {
            closeMetricOnly(ctx, "cleartext_invalid");
            return;
        }
        String fileId = new String(p, 2, slen, StandardCharsets.UTF_8);
        nativeOps.cancelFile(caller, fileId);
    }

    private void cleartextRtcStart(ChannelHandlerContext ctx, ZspFrame frame, byte[] caller, byte[] p, int defaultKind) {
        Optional<ZspPayloadReaders.PeerCall> pc = ZspPayloadReaders.parsePeerAndCallKind(p);
        int kind = defaultKind;
        byte[] peer;
        if (pc.isPresent()) {
            kind = pc.get().callKind();
            peer = pc.get().peerUserId16();
        } else if (p.length >= ZspConstants.USER_ID_SIZE) {
            peer = Arrays.copyOfRange(p, 0, ZspConstants.USER_ID_SIZE);
        } else {
            closeMetricOnly(ctx, "cleartext_invalid");
            return;
        }
        byte[] callId = nativeOps.rtcStartCall(caller, peer, kind);
        if (callId != null && callId.length > 0) {
            ZspFrame out = outbound.replySameFlags(ctx, frame, frame.header().messageType(), callId);
            writeFrame(ctx, out);
        }
    }

    private void cleartextCallSignal(ChannelHandlerContext ctx, byte[] caller, byte[] p) {
        Optional<ZspPayloadReaders.CallSignal> cs = ZspPayloadReaders.parseCallSignal(p);
        if (cs.isEmpty()) {
            closeMetricOnly(ctx, "cleartext_invalid");
            return;
        }
        byte[] data = cs.get().data();
        Optional<byte[]> callIdOpt = ZspPayloadReaders.extractCallIdFromSignalData(data);
        if (callIdOpt.isEmpty()) {
            closeMetricOnly(ctx, "cleartext_invalid");
            return;
        }
        byte[] callId = callIdOpt.get();
        int st = cs.get().subType();
        switch (st) {
            case 2 -> nativeOps.rtcAcceptCall(caller, callId);
            case 3 -> nativeOps.rtcRejectCall(caller, callId);
            case 4 -> nativeOps.rtcEndCall(caller, callId);
            default -> { /* ringing/ice: native optional */ }
        }
    }

    private void cleartextFriendRequest(ChannelHandlerContext ctx, ZspFrame frame, byte[] caller, byte[] p) {
        if (p.length < 16 + 16 + 8 + 64) {
            closeMetricOnly(ctx, "cleartext_invalid");
            return;
        }
        ByteBuffer buf = ByteBuffer.wrap(p).order(ByteOrder.BIG_ENDIAN);
        byte[] from = new byte[16];
        byte[] to = new byte[16];
        buf.get(from);
        buf.get(to);
        long ts = buf.getLong();
        byte[] sig = new byte[64];
        buf.get(sig);
        byte[] req = nativeOps.sendFriendRequest(caller, from, to, ts, sig);
        if (req != null && req.length > 0) {
            writeFrame(ctx, outbound.replySameFlags(ctx, frame, MessageTypes.FRIEND_REQUEST, req));
        } else {
            writeFrame(ctx, outbound.replySameFlags(ctx, frame, MessageTypes.FRIEND_REQUEST, new byte[0]));
        }
    }

    private void cleartextFriendResponse(ChannelHandlerContext ctx, ZspFrame frame, byte[] caller, byte[] p) {
        if (p.length < 16 + 1 + 16 + 8 + 64) {
            closeMetricOnly(ctx, "cleartext_invalid");
            return;
        }
        ByteBuffer buf = ByteBuffer.wrap(p).order(ByteOrder.BIG_ENDIAN);
        byte[] reqId = new byte[16];
        buf.get(reqId);
        boolean acc = buf.get() != 0;
        byte[] resp = new byte[16];
        buf.get(resp);
        long ts = buf.getLong();
        byte[] sig = new byte[64];
        buf.get(sig);
        boolean ok = nativeOps.respondFriendRequest(caller, reqId, acc, resp, ts, sig);
        byte[] reply = new byte[] {(byte) (ok ? 1 : 0)};
        writeFrame(ctx, outbound.replySameFlags(ctx, frame, MessageTypes.FRIEND_RESPONSE, reply));
    }

    private void cleartextDeleteFriend(ChannelHandlerContext ctx, ZspFrame frame, byte[] caller, byte[] p) {
        if (p.length < 16 + 16 + 8 + 64) {
            closeMetricOnly(ctx, "cleartext_invalid");
            return;
        }
        ByteBuffer buf = ByteBuffer.wrap(p).order(ByteOrder.BIG_ENDIAN);
        byte[] user = new byte[16];
        byte[] friend = new byte[16];
        buf.get(user);
        buf.get(friend);
        long ts = buf.getLong();
        byte[] sig = new byte[64];
        buf.get(sig);
        boolean ok = nativeOps.deleteFriend(caller, user, friend, ts, sig);
        byte[] reply = new byte[] {(byte) (ok ? 1 : 0)};
        writeFrame(ctx, outbound.replySameFlags(ctx, frame, MessageTypes.DELETE_FRIEND, reply));
    }

    private void cleartextFriendNote(ChannelHandlerContext ctx, ZspFrame frame, byte[] caller, byte[] p) {
        nativeOps.storeMessage(caller, extractImSession(p), p);
    }

    private void cleartextGroupCreate(ChannelHandlerContext ctx, ZspFrame frame, byte[] caller, byte[] p) {
        Optional<String> name = ZspPayloadReaders.parseUtf8Prefixed(p);
        if (name.isEmpty()) {
            closeMetricOnly(ctx, "cleartext_invalid");
            return;
        }
        byte[] creator = ctx.channel().attr(ZspChannelAttributes.AUTH_USER_ID_16).get();
        if (creator == null || creator.length != ZspConstants.USER_ID_SIZE) {
            closeMetricOnly(ctx, "cleartext_invalid");
            return;
        }
        byte[] gid = nativeOps.createGroup(caller, creator, name.get());
        if (gid != null && gid.length > 0) {
            writeFrame(ctx, outbound.replySameFlags(ctx, frame, MessageTypes.GROUP_CREATE, gid));
        }
    }

    private void cleartextGroupPair(ChannelHandlerContext ctx, ZspFrame frame, byte[] caller, int type, byte[] p) {
        Optional<ZspPayloadReaders.GroupPair> gp = ZspPayloadReaders.parseGroupPair(p);
        if (gp.isEmpty()) {
            closeMetricOnly(ctx, "cleartext_invalid");
            return;
        }
        byte[] gid = gp.get().idA16();
        byte[] uid = gp.get().idB16();
        switch (type) {
            case MessageTypes.GROUP_INVITE -> nativeOps.inviteMember(caller, gid, uid);
            case MessageTypes.GROUP_REMOVE -> nativeOps.removeMember(caller, gid, uid);
            case MessageTypes.GROUP_LEAVE -> nativeOps.leaveGroup(caller, gid, uid);
            default -> { }
        }
    }

    private void cleartextGroupNameUpdate(ChannelHandlerContext ctx, ZspFrame frame, byte[] caller, byte[] p) {
        if (p.length < 32 + 2) {
            closeMetricOnly(ctx, "cleartext_invalid");
            return;
        }
        byte[] gid = Arrays.copyOfRange(p, 0, 16);
        byte[] upd = Arrays.copyOfRange(p, 16, 32);
        Optional<String> name = ZspPayloadReaders.parseUtf8Prefixed(Arrays.copyOfRange(p, 32, p.length));
        if (name.isEmpty()) {
            closeMetricOnly(ctx, "cleartext_invalid");
            return;
        }
        nativeOps.updateGroupName(caller, gid, upd, name.get(), System.currentTimeMillis());
    }

    private static byte[] extractImSession(byte[] payload) {
        if (payload == null || payload.length < ZspConstants.USER_ID_SIZE) {
            return new byte[ZspConstants.USER_ID_SIZE];
        }
        return Arrays.copyOfRange(payload, 0, ZspConstants.USER_ID_SIZE);
    }

    private static boolean isAllZero(byte[] b) {
        if (b == null) {
            return true;
        }
        for (byte x : b) {
            if (x != 0) {
                return false;
            }
        }
        return true;
    }

    private void forwardFrame(ChannelHandlerContext srcCtx, ZspFrame request, byte[] toUserId16) {
        Channel ch = registry.findChannel(toUserId16);
        if (ch == null || !ch.isActive()) {
            if (properties.isOfflineQueueEnabled()) {
                offlineQueue.enqueue(
                        toUserId16,
                        request.header().sessionId(),
                        request.header().messageType(),
                        request.header().flags(),
                        request.payload());
            }
            return;
        }
        ChannelHandlerContext dst = ch.pipeline().context(ZspInboundHandler.class);
        if (dst == null) {
            return;
        }
        ZspFrame out =
                outbound.replySameFlags(dst, request, request.header().messageType(), request.payload());
        writeFrame(dst, out);
        if (metrics != null) {
            metrics.recordForwardDelivered();
        }
    }

    private void closeDiag(ChannelHandlerContext ctx, Level level, String code) {
        if (metrics != null) {
            metrics.recordClose(code);
        }
        logInboundEvent(level, code, ctx);
        ZspGatewayLog.diag(LOG, properties, level, code);
        ctx.close();
    }

    private void closeMetricOnly(ChannelHandlerContext ctx, String code) {
        if (metrics != null) {
            metrics.recordClose(code);
        }
        logInboundEvent(Level.INFO, code, ctx);
        ctx.close();
    }

    /** 与 {@link ZspServerProperties#isLogInboundEvents()} 对齐；不含业务载荷。 */
    private void logInboundEvent(Level level, String code, ChannelHandlerContext ctx) {
        if (properties == null || !properties.isLogInboundEvents()) {
            return;
        }
        LOG.log(level, "[zsp] {0} peer={1}", new Object[] {code, String.valueOf(ctx.channel().remoteAddress())});
    }

    private static Optional<LocalRegisterPayload> parseLocalRegisterPayload(byte[] p) {
        if (p == null || p.length < 16 + 2 + 2) {
            return Optional.empty();
        }
        ByteBuffer buf = ByteBuffer.wrap(p).order(ByteOrder.BIG_ENDIAN);
        byte[] uid = new byte[ZspConstants.USER_ID_SIZE];
        buf.get(uid);
        int pwLen = buf.getShort() & 0xFFFF;
        if (pwLen < 8 || pwLen > 512 || buf.remaining() < pwLen + 2) {
            return Optional.empty();
        }
        byte[] pw = new byte[pwLen];
        buf.get(pw);
        int recLen = buf.getShort() & 0xFFFF;
        if (recLen < 32 || recLen > 8192 || buf.remaining() < recLen) {
            return Optional.empty();
        }
        byte[] rec = new byte[recLen];
        buf.get(rec);
        if (buf.hasRemaining()) {
            return Optional.empty();
        }
        return Optional.of(new LocalRegisterPayload(uid, pw, rec));
    }

    private static Optional<LocalPasswordPayload> parseLocalPasswordPayload(byte[] p) {
        if (p == null || p.length < 16 + 2) {
            return Optional.empty();
        }
        ByteBuffer buf = ByteBuffer.wrap(p).order(ByteOrder.BIG_ENDIAN);
        byte[] uid = new byte[ZspConstants.USER_ID_SIZE];
        buf.get(uid);
        int pwLen = buf.getShort() & 0xFFFF;
        if (pwLen < 8 || pwLen > 512 || buf.remaining() != pwLen) {
            return Optional.empty();
        }
        byte[] pw = new byte[pwLen];
        buf.get(pw);
        return Optional.of(new LocalPasswordPayload(uid, pw));
    }

    private record LocalRegisterPayload(byte[] userId, byte[] password, byte[] recovery) {}

    private record LocalPasswordPayload(byte[] userId, byte[] password) {}

    private static byte[] remoteIpBytes(ChannelHandlerContext ctx) {
        if (!(ctx.channel().remoteAddress() instanceof java.net.InetSocketAddress inet)) {
            return new byte[0];
        }
        var addr = inet.getAddress();
        return addr != null ? addr.getAddress() : new byte[0];
    }

    public void onChannelInactive(byte[] userId16) {
        if (userId16 != null) {
            registry.unregister(userId16);
        }
    }
}
