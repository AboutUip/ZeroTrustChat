package com.ztrust.zchat.im.zsp.meta;

/**
 * ZSP Meta 段（变长，首 2 字节为 MetaTotalLength）。
 */
public record ZspMetaSection(int totalLength, long timestampMs, byte[] nonce12, int keyId) {

    public ZspMetaSection {
        if (nonce12 == null || nonce12.length != 12) {
            throw new IllegalArgumentException("nonce must be 12 bytes");
        }
    }
}
