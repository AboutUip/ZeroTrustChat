package com.ztrust.zchat.im.zsp;

import io.netty.buffer.ByteBuf;
import io.netty.channel.ChannelHandlerContext;
import io.netty.handler.codec.MessageToByteEncoder;

public final class ZspFrameEncoder extends MessageToByteEncoder<ZspFrame> {

    @Override
    protected void encode(ChannelHandlerContext ctx, ZspFrame msg, ByteBuf out) {
        if (msg.header().payloadLength() != msg.payload().length) {
            throw new IllegalArgumentException(
                    "header.payloadLength must equal payload.length: "
                            + msg.header().payloadLength()
                            + " vs "
                            + msg.payload().length);
        }
        msg.header().writeTo(out);
        out.writeBytes(msg.meta());
        out.writeBytes(msg.payload());
        out.writeBytes(msg.authTag());
    }
}
