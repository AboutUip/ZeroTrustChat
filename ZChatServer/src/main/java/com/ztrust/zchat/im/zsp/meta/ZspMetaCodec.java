package com.ztrust.zchat.im.zsp.meta;

import com.ztrust.zchat.im.zsp.ZspConstants;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;

public final class ZspMetaCodec {

    private ZspMetaCodec() {}

    public static ZspMetaSection parse(byte[] meta) {
        if (meta == null || meta.length < ZspConstants.MIN_META_LENGTH) {
            throw new IllegalArgumentException("meta too short");
        }
        ByteBuffer buf = ByteBuffer.wrap(meta).order(ByteOrder.BIG_ENDIAN);
        int total = buf.getShort() & 0xFFFF;
        if (total != meta.length) {
            throw new IllegalArgumentException("MetaLength prefix mismatch");
        }
        long ts = buf.getLong();
        byte[] nonce = new byte[12];
        buf.get(nonce);
        int keyId = buf.getInt();
        return new ZspMetaSection(total, ts, nonce, keyId);
    }

    public static byte[] encodeMinimal(long timestampMs, byte[] nonce12, int keyId) {
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
