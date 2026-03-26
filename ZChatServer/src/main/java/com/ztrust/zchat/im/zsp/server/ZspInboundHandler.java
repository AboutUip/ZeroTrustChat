package com.ztrust.zchat.im.zsp.server;

import com.ztrust.zchat.im.zsp.ZspFrame;
import io.netty.channel.ChannelHandler;
import io.netty.channel.ChannelHandlerContext;
import io.netty.channel.SimpleChannelInboundHandler;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.stereotype.Component;

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
        if (metrics != null) {
            metrics.recordClose("pipeline_error");
        }
        ZspGatewayLog.diag(LOG, props, Level.WARNING, "pipeline_error");
        ctx.close();
    }
}
