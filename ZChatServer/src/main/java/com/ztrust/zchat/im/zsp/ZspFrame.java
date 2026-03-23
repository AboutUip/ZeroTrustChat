package com.ztrust.zchat.im.zsp;

public record ZspFrame(ZspHeader header, byte[] meta, byte[] payload, byte[] authTag) {

    public ZspFrame {
        if (meta == null || payload == null || authTag == null) {
            throw new IllegalArgumentException("null frame component");
        }
        if (authTag.length != ZspConstants.AUTH_TAG_LENGTH) {
            throw new IllegalArgumentException("authTag must be " + ZspConstants.AUTH_TAG_LENGTH + " bytes");
        }
    }
}
