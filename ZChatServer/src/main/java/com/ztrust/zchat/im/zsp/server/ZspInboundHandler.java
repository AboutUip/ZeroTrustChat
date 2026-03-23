package com.ztrust.zchat.im.zsp.server;

import com.ztrust.zchat.im.zsp.MessageTypes;
import com.ztrust.zchat.im.zsp.ZspAuthPayload;
import com.ztrust.zchat.im.zsp.ZspConstants;
import com.ztrust.zchat.im.zsp.ZspFrame;
import com.ztrust.zchat.im.zsp.ZspHeader;
import com.ztrust.zchat.im.zsp.ZspMeta;
import io.netty.channel.ChannelHandler;
import io.netty.channel.ChannelHandlerContext;
import io.netty.channel.SimpleChannelInboundHandler;
import org.springframework.stereotype.Component;

import java.net.InetSocketAddress;
import java.util.concurrent.ThreadLocalRandom;
import java.util.logging.Level;
import java.util.logging.Logger;

@Component
@ChannelHandler.Sharable
public final class ZspInboundHandler extends SimpleChannelInboundHandler<ZspFrame> {

    private static final Logger LOG = Logger.getLogger(ZspInboundHandler.class.getName());

    private final ZspServerProperties properties;
    private final ZspJniBridge jni;

    public ZspInboundHandler(ZspServerProperties properties, ZspJniBridge jni) {
        this.properties = properties;
        this.jni = jni;
    }

    @Override
    public void channelActive(ChannelHandlerContext ctx) throws Exception {
        ctx.channel().attr(ZspChannelAttributes.OUTBOUND_SEQUENCE).set(1L);
        super.channelActive(ctx);
    }

    @Override
    protected void channelRead0(ChannelHandlerContext ctx, ZspFrame frame) {
        int type = frame.header().messageType();
        if (type == MessageTypes.HEARTBEAT) {
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
        if (LOG.isLoggable(Level.FINE)) {
            LOG.log(Level.FINE, "ZSP message type=0x{0} payloadLen={1}", new Object[] {
                Integer.toHexString(type), frame.payload().length
            });
        }
    }

    private void handleAuth(ChannelHandlerContext ctx, ZspFrame frame) {
        if (!properties.isJniEnabled()) {
            LOG.warning("AUTH received but zchat.zsp.jni-enabled=false; closing channel");
            ctx.close();
            return;
        }
        var parsed = ZspAuthPayload.parse(frame.payload());
        if (parsed.isEmpty()) {
            LOG.warning("Invalid AUTH payload (§6.7)");
            ctx.close();
            return;
        }
        ZspAuthPayload p = parsed.get();
        byte[] ip = remoteIpBytes(ctx);
        byte[] session = jni.auth(p.userId(), p.token(), ip);
        if (session == null || session.length == 0) {
            LOG.info("AUTH failed");
            ctx.close();
            return;
        }
        if (session.length > ZspConstants.MAX_PAYLOAD_LENGTH_U16) {
            LOG.warning("AUTH session opaque longer than ZSP header uint16; closing");
            ctx.close();
            return;
        }
        ctx.channel().attr(ZspChannelAttributes.CALLER_SESSION_ID).set(session);
        ZspFrame reply = replyFrame(ctx, frame, MessageTypes.AUTH, session);
        ctx.writeAndFlush(reply);
    }

    private void handleLogout(ChannelHandlerContext ctx) {
        byte[] sid = ctx.channel().attr(ZspChannelAttributes.CALLER_SESSION_ID).get();
        if (properties.isJniEnabled() && sid != null) {
            jni.destroyCallerSession(sid);
        }
        ctx.channel().attr(ZspChannelAttributes.CALLER_SESSION_ID).set(null);
        ctx.close();
    }

    private ZspFrame replyFrame(ChannelHandlerContext ctx, ZspFrame request, int messageType, byte[] payload) {
        Long cur = ctx.channel().attr(ZspChannelAttributes.OUTBOUND_SEQUENCE).get();
        long seq = cur != null ? cur : 1L;
        ctx.channel().attr(ZspChannelAttributes.OUTBOUND_SEQUENCE).set(seq + 1);
        byte[] nonce = new byte[12];
        ThreadLocalRandom.current().nextBytes(nonce);
        byte[] meta = ZspMeta.minimal(System.currentTimeMillis(), nonce, 0);
        byte[] tag = new byte[ZspConstants.AUTH_TAG_LENGTH];
        ZspHeader h = new ZspHeader(
                ZspConstants.MAGIC,
                ZspConstants.PROTOCOL_VERSION,
                messageType,
                request.header().flags(),
                0,
                request.header().sessionId(),
                seq,
                payload.length);
        return new ZspFrame(h, meta, payload, tag);
    }

    private static byte[] remoteIpBytes(ChannelHandlerContext ctx) {
        if (!(ctx.channel().remoteAddress() instanceof InetSocketAddress inet)) {
            return new byte[0];
        }
        var addr = inet.getAddress();
        return addr != null ? addr.getAddress() : new byte[0];
    }

    @Override
    public void exceptionCaught(ChannelHandlerContext ctx, Throwable cause) {
        LOG.log(Level.WARNING, "ZSP pipeline error", cause);
        ctx.close();
    }
}
