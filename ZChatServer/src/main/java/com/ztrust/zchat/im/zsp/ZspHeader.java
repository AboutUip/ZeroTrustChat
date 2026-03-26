package com.ztrust.zchat.im.zsp;

import io.netty.buffer.ByteBuf;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;

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

    /** 与 on-wire Header 16 字节一致（用于 HMAC 等完整性计算）。 */
    public byte[] toBytesBigEndian() {
        ByteBuffer buf = ByteBuffer.allocate(16).order(ByteOrder.BIG_ENDIAN);
        buf.putShort((short) magic);
        buf.put((byte) version);
        buf.put((byte) messageType);
        buf.put((byte) flags);
        buf.put((byte) reserved);
        buf.putInt((int) (sessionId & 0xFFFFFFFFL));
        buf.putInt((int) (sequence & 0xFFFFFFFFL));
        buf.putShort((short) payloadLength);
        return buf.array();
    }
}
