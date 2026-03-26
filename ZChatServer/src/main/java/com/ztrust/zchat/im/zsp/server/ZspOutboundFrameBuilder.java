package com.ztrust.zchat.im.zsp.server;

import com.ztrust.zchat.im.zsp.ZspConstants;
import com.ztrust.zchat.im.zsp.ZspFrame;
import com.ztrust.zchat.im.zsp.ZspHeader;
import com.ztrust.zchat.im.zsp.ZspMeta;
import com.ztrust.zchat.im.zsp.security.ZspFrameTagService;
import io.netty.channel.ChannelHandlerContext;
import io.netty.util.Attribute;
import org.springframework.stereotype.Component;

import java.util.concurrent.ThreadLocalRandom;

/**
 * 统一构造出站帧（序列号、Meta、Auth Tag）。
 */
@Component
public final class ZspOutboundFrameBuilder {

    private final ZspFrameTagService tagService;

    public ZspOutboundFrameBuilder(ZspFrameTagService tagService) {
        this.tagService = tagService;
    }

    public ZspFrame replySameFlags(ChannelHandlerContext ctx, ZspFrame request, int messageType, byte[] payload) {
        return reply(ctx, request, messageType, request.header().flags(), payload);
    }

    public ZspFrame reply(ChannelHandlerContext ctx, ZspFrame request, int messageType, int flags, byte[] payload) {
        long seq = nextOutboundSeq(ctx);
        byte[] nonce = new byte[12];
        ThreadLocalRandom.current().nextBytes(nonce);
        byte[] meta = ZspMeta.minimal(System.currentTimeMillis(), nonce, 0);
        ZspHeader h = new ZspHeader(
                ZspConstants.MAGIC,
                ZspConstants.PROTOCOL_VERSION,
                messageType,
                flags,
                0,
                request.header().sessionId(),
                seq,
                payload.length);
        byte[] tag = tagService.outboundTag(h, meta, payload);
        return new ZspFrame(h, meta, payload, tag);
    }

    private static long nextOutboundSeq(ChannelHandlerContext ctx) {
        Attribute<Long> attr = ctx.channel().attr(ZspChannelAttributes.OUTBOUND_SEQUENCE);
        Long cur = attr.get();
        long seq = cur != null ? cur : 1L;
        attr.set(seq + 1);
        return seq;
    }
}
