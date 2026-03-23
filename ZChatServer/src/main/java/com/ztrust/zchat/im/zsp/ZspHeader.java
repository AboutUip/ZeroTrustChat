package com.ztrust.zchat.im.zsp;

import io.netty.buffer.ByteBuf;

public record ZspHeader(
        int magic,
        int version,
        int messageType,
        int flags,
        int reserved,
        long sessionId,
        long sequence,
        int payloadLength) {

    public static ZspHeader read(ByteBuf buf) {
        int magic = buf.readUnsignedShort();
        int version = buf.readUnsignedByte();
        int messageType = buf.readUnsignedByte();
        int flags = buf.readUnsignedByte();
        int reserved = buf.readUnsignedByte();
        long sessionId = buf.readUnsignedInt();
        long sequence = buf.readUnsignedInt();
        int payloadLength = buf.readUnsignedShort();
        return new ZspHeader(magic, version, messageType, flags, reserved, sessionId, sequence, payloadLength);
    }

    public void writeTo(ByteBuf out) {
        out.writeShort(magic);
        out.writeByte(version);
        out.writeByte(messageType);
        out.writeByte(flags);
        out.writeByte(reserved);
        out.writeInt((int) (sessionId & 0xFFFFFFFFL));
        out.writeInt((int) (sequence & 0xFFFFFFFFL));
        out.writeShort(payloadLength);
    }
}
