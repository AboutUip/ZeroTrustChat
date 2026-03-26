package com.ztrust.zchat.im.zsp;

import com.ztrust.zchat.im.zsp.meta.ZspMetaCodec;

public final class ZspMeta {

    private ZspMeta() {}

    public static byte[] minimal(long timestampMs, byte[] nonce12, int keyId) {
        return ZspMetaCodec.encodeMinimal(timestampMs, nonce12, keyId);
    }
}
