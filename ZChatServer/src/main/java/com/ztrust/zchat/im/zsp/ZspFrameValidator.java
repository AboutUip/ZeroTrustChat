package com.ztrust.zchat.im.zsp;

import com.ztrust.zchat.im.zsp.meta.ZspMetaCodec;
import com.ztrust.zchat.im.zsp.meta.ZspMetaSection;

public final class ZspFrameValidator {

    private ZspFrameValidator() {}

    public static void validateOrThrow(ZspFrame frame) {
        ZspHeader h = frame.header();
        if (h.magic() != ZspConstants.MAGIC) {
            throw new ZspCodecException("bad magic");
        }
        if (h.version() != ZspConstants.PROTOCOL_VERSION) {
            throw new ZspCodecException("unsupported protocol version: " + h.version());
        }
        if (h.payloadLength() != frame.payload().length) {
            throw new ZspCodecException("header length vs payload mismatch");
        }
        if (frame.meta().length < ZspConstants.MIN_META_LENGTH
                || frame.meta().length > ZspConstants.MAX_META_LENGTH) {
            throw new ZspCodecException("meta length out of range");
        }
        ZspMetaSection m;
        try {
            m = ZspMetaCodec.parse(frame.meta());
        } catch (IllegalArgumentException e) {
            throw new ZspCodecException("invalid meta: " + e.getMessage());
        }
        if (m.totalLength() != frame.meta().length) {
            throw new ZspCodecException("meta self-length mismatch");
        }
        if (frame.authTag().length != ZspConstants.AUTH_TAG_LENGTH) {
            throw new ZspCodecException("auth tag length");
        }
    }
}
