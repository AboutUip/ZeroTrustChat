package com.ztrust.zchat.im.zsp.tlv;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.Optional;

/**
 * 解析 Payload 尾部 TLV 扩展（见 02-ZSP-Protocol.md 第七节）。
 */
public final class ZspTlvIterator {

    private final ByteBuffer buf;

    public ZspTlvIterator(byte[] payload, int offset) {
        if (offset < 0 || offset > payload.length) {
            throw new IllegalArgumentException("bad offset");
        }
        this.buf = ByteBuffer.wrap(payload, offset, payload.length - offset).order(ByteOrder.BIG_ENDIAN);
    }

    public Optional<Tlv> next() {
        if (buf.remaining() < 3) {
            return Optional.empty();
        }
        int type = buf.get() & 0xFF;
        int len = buf.getShort() & 0xFFFF;
        if (len < 0 || buf.remaining() < len) {
            return Optional.empty();
        }
        byte[] value = new byte[len];
        buf.get(value);
        return Optional.of(new Tlv(type, value));
    }

    public record Tlv(int type, byte[] value) {}
}
