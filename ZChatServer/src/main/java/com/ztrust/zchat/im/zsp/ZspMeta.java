package com.ztrust.zchat.im.zsp;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;

public final class ZspMeta {

    private ZspMeta() {}

    public static byte[] minimal(long timestampMs, byte[] nonce12, int keyId) {
        if (nonce12 == null || nonce12.length != 12) {
            throw new IllegalArgumentException("nonce must be 12 bytes");
        }
        ByteBuffer buf = ByteBuffer.allocate(ZspConstants.MIN_META_LENGTH).order(ByteOrder.BIG_ENDIAN);
        buf.putShort((short) ZspConstants.MIN_META_LENGTH);
        buf.putLong(timestampMs);
        buf.put(nonce12);
        buf.putInt(keyId);
        return buf.array();
    }
}
