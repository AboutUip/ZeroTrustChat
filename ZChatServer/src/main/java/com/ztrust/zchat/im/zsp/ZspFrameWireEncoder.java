package com.ztrust.zchat.im.zsp;

import io.netty.buffer.ByteBuf;
import io.netty.buffer.ByteBufAllocator;

/**
 * 将 {@link ZspFrame} 编码为 Netty {@link ByteBuf}，在业务侧调用 {@link io.netty.channel.ChannelHandlerContext#writeAndFlush(Object)}
 * 写出。
 *
 * <p>不在管道里传递 {@code ZspFrame}，可避免 Spring Boot DevTools 双类加载器、以及出站顺序导致编码器未执行等问题。
 */
public final class ZspFrameWireEncoder {

    private ZspFrameWireEncoder() {}

    public static ByteBuf toByteBuf(ByteBufAllocator alloc, ZspFrame frame) {
        if (frame.header().payloadLength() != frame.payload().length) {
            throw new IllegalArgumentException(
                    "header.payloadLength must equal payload.length: "
                            + frame.header().payloadLength()
                            + " vs "
                            + frame.payload().length);
        }
        ByteBuf out = alloc.buffer();
        frame.header().writeTo(out);
        out.writeBytes(frame.meta());
        out.writeBytes(frame.payload());
        out.writeBytes(frame.authTag());
        return out;
    }
}
