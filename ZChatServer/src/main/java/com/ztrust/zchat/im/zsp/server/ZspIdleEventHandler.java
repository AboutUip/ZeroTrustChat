package com.ztrust.zchat.im.zsp.server;

import io.netty.channel.ChannelHandlerContext;
import io.netty.channel.ChannelInboundHandlerAdapter;
import io.netty.handler.timeout.IdleState;
import io.netty.handler.timeout.IdleStateEvent;

public final class ZspIdleEventHandler extends ChannelInboundHandlerAdapter {

    @Override
    public void userEventTriggered(ChannelHandlerContext ctx, Object evt) {
        if (evt instanceof IdleStateEvent idle && idle.state() == IdleState.READER_IDLE) {
            ctx.close();
            return;
        }
        ctx.fireUserEventTriggered(evt);
    }
}
