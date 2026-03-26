package com.ztrust.zchat.im.zsp;

import io.netty.buffer.ByteBuf;
import io.netty.channel.ChannelHandlerContext;
import io.netty.handler.codec.ByteToMessageDecoder;

import java.util.List;

public final class ZspFrameDecoder extends ByteToMessageDecoder {

    @Override
    protected void decode(ChannelHandlerContext ctx, ByteBuf in, List<Object> out) {
        if (in.readableBytes() < ZspConstants.HEADER_LENGTH) {
            return;
        }
        int start = in.readerIndex();
        if (in.getUnsignedShort(start) != ZspConstants.MAGIC) {
            throw new ZspCodecException("Invalid ZSP magic");
        }
        int ver = in.getUnsignedByte(start + 2);
        if (ver != ZspConstants.PROTOCOL_VERSION) {
            throw new ZspCodecException("Unsupported ZSP version: " + ver);
        }
        int payloadLen = in.getUnsignedShort(start + 14);
        int metaLen = in.getUnsignedShort(start + 16);
        if (metaLen < ZspConstants.MIN_META_LENGTH || metaLen > ZspConstants.MAX_META_LENGTH) {
            throw new ZspCodecException("Invalid MetaLength: " + metaLen);
        }
        if (payloadLen > ZspConstants.MAX_PAYLOAD_LENGTH_U16) {
            throw new ZspCodecException("Payload length exceeds header uint16: " + payloadLen);
        }
        int total = ZspConstants.HEADER_LENGTH + metaLen + payloadLen + ZspConstants.AUTH_TAG_LENGTH;
        if (in.readableBytes() < total) {
            return;
        }
        ByteBuf slice = in.readSlice(total);
        ZspHeader header = ZspHeader.read(slice);
        if (header.payloadLength() != payloadLen) {
            throw new ZspCodecException("Header Length field inconsistent with frame");
        }
        byte[] meta = new byte[metaLen];
        slice.readBytes(meta);
        int metaDeclared = ((meta[0] & 0xFF) << 8) | (meta[1] & 0xFF);
        if (metaDeclared != metaLen) {
            throw new ZspCodecException("MetaLength prefix does not match Meta section size");
        }
        byte[] payload = payloadLen > 0 ? new byte[payloadLen] : new byte[0];
        if (payloadLen > 0) {
            slice.readBytes(payload);
        }
        byte[] tag = new byte[ZspConstants.AUTH_TAG_LENGTH];
        slice.readBytes(tag);
        out.add(new ZspFrame(header, meta, payload, tag));
    }
}
