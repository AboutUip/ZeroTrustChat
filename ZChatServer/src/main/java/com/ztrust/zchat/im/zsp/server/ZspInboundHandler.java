package com.ztrust.zchat.im.zsp.server;

import com.ztrust.zchat.im.zsp.ZspFrame;
import io.netty.channel.ChannelHandler;
import io.netty.channel.ChannelHandlerContext;
import io.netty.channel.SimpleChannelInboundHandler;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.stereotype.Component;

import java.io.IOException;
import java.net.SocketException;
import java.nio.channels.ClosedChannelException;
import java.util.logging.Level;
import java.util.logging.Logger;

@Component
@ChannelHandler.Sharable
public final class ZspInboundHandler extends SimpleChannelInboundHandler<ZspFrame> {

    private static final Logger LOG = Logger.getLogger(ZspInboundHandler.class.getName());

    private final ZspMessageDispatcher dispatcher;
    private final ZspServerProperties props;
    private final ZspMetrics metrics;

    public ZspInboundHandler(
            ZspMessageDispatcher dispatcher,
            ZspServerProperties props,
            @Autowired(required = false) ZspMetrics metrics) {
        this.dispatcher = dispatcher;
        this.props = props;
        this.metrics = metrics;
    }

    @Override
    public void channelActive(ChannelHandlerContext ctx) throws Exception {
        ctx.channel().attr(ZspChannelAttributes.OUTBOUND_SEQUENCE).set(1L);
        if (metrics != null) {
            metrics.tcpConnected();
        }
        if (props.isLogInboundEvents()) {
            LOG.info("[zsp] tcp_open peer=" + ctx.channel().remoteAddress());
        }
        super.channelActive(ctx);
    }

    @Override
    public void channelInactive(ChannelHandlerContext ctx) throws Exception {
        if (metrics != null) {
            metrics.tcpDisconnected();
        }
        byte[] uid = ctx.channel().attr(ZspChannelAttributes.AUTH_USER_ID_16).get();
        dispatcher.onChannelInactive(uid);
        super.channelInactive(ctx);
    }

    @Override
    protected void channelRead0(ChannelHandlerContext ctx, ZspFrame frame) {
        if (props.isDiagnosticLogging() && LOG.isLoggable(Level.FINE)) {
            LOG.log(
                    Level.FINE,
                    "ZSP message type=0x{0} payloadLen={1}",
                    new Object[] {Integer.toHexString(frame.header().messageType()), frame.payload().length});
        }
        dispatcher.dispatch(ctx, frame);
    }

    @Override
    public void exceptionCaught(ChannelHandlerContext ctx, Throwable cause) {
        boolean benignReset = isBenignPeerTransportClose(cause);
        if (metrics != null) {
            metrics.recordClose(benignReset ? "peer_reset" : "pipeline_error");
        }
        if (benignReset) {
            // 对端或网络 RST：移动网络/NAT/杀进程常见，不作为解码/业务异常告警
            if (props.isLogInboundEvents()) {
                LOG.info(
                        "[zsp] tcp_reset peer="
                                + ctx.channel().remoteAddress()
                                + " (connection reset / broken pipe)");
            } else if (props.isDiagnosticLogging() && LOG.isLoggable(Level.FINE)) {
                LOG.log(
                        Level.FINE,
                        "[zsp] tcp_reset peer=" + ctx.channel().remoteAddress(),
                        cause);
            }
            ZspGatewayLog.diag(LOG, props, Level.FINE, "peer_reset");
        } else {
            // 解码/管道异常打 WARNING，便于在 diagnostic-logging=false 时仍能看到原因（如 ZspCodecException）
            LOG.log(
                    Level.WARNING,
                    "[zsp] pipeline_error peer=" + ctx.channel().remoteAddress(),
                    cause);
            ZspGatewayLog.diag(LOG, props, Level.WARNING, "pipeline_error");
        }
        ctx.close();
    }

    /**
     * 对端主动 RST、本端已关通道、broken pipe 等，属正常断线形态，与协议解析错误区分。
     */
    private static boolean isBenignPeerTransportClose(Throwable cause) {
        for (Throwable t = cause; t != null; t = t.getCause()) {
            if (t instanceof ClosedChannelException) {
                return true;
            }
            if (t instanceof SocketException) {
                return matchesTransportResetMessage(t.getMessage());
            }
            if (t instanceof IOException) {
                return matchesTransportResetMessage(t.getMessage());
            }
            String cn = t.getClass().getName();
            if (cn.endsWith("NativeIoException")) {
                return matchesTransportResetMessage(t.getMessage());
            }
        }
        return false;
    }

    private static boolean matchesTransportResetMessage(String message) {
        if (message == null) {
            return false;
        }
        String s = message.toLowerCase();
        return s.contains("connection reset")
                || s.contains("broken pipe")
                || s.contains("connection aborted");
    }
}
