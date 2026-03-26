package com.ztrust.zchat.im.zsp.security;

import com.ztrust.zchat.im.zsp.ZspConstants;
import com.ztrust.zchat.im.zsp.ZspFrame;
import com.ztrust.zchat.im.zsp.ZspHeader;
import org.springframework.stereotype.Component;

import javax.crypto.Mac;
import javax.crypto.spec.SecretKeySpec;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.Arrays;
import java.util.logging.Level;
import java.util.logging.Logger;

import com.ztrust.zchat.im.zsp.server.ZspGatewayLog;
import com.ztrust.zchat.im.zsp.server.ZspServerProperties;

/**
 * 出站 Auth Tag 计算与入站校验（可选）。
 */
@Component
public final class ZspFrameTagService {

    private static final Logger LOG = Logger.getLogger(ZspFrameTagService.class.getName());

    private final ZspServerProperties props;
    private volatile byte[] hmacKey32;

    public ZspFrameTagService(ZspServerProperties props) {
        this.props = props;
    }

    public byte[] outboundTag(ZspHeader header, byte[] meta, byte[] payload) {
        ZspFrameTagMode mode = props.getFrameTagMode();
        if (mode == ZspFrameTagMode.NONE) {
            return new byte[ZspConstants.AUTH_TAG_LENGTH];
        }
        if (mode == ZspFrameTagMode.HMAC_SHA256_128) {
            return hmac128(ensureKey(), header, meta, payload);
        }
        return new byte[ZspConstants.AUTH_TAG_LENGTH];
    }

    public boolean verifyInbound(ZspFrame frame) {
        ZspFrameTagMode mode = props.getFrameTagMode();
        if (mode != ZspFrameTagMode.HMAC_SHA256_128) {
            return true;
        }
        if (!props.isVerifyInboundAuthTag()) {
            return true;
        }
        try {
            byte[] expect = hmac128(ensureKey(), frame.header(), frame.meta(), frame.payload());
            return MessageDigest.isEqual(expect, frame.authTag());
        } catch (RuntimeException e) {
            ZspGatewayLog.diag(LOG, props, Level.WARNING, "inbound_auth_tag_verify_exception");
            return false;
        }
    }

    private byte[] ensureKey() {
        byte[] k = hmacKey32;
        if (k != null) {
            return k;
        }
        synchronized (this) {
            if (hmacKey32 != null) {
                return hmacKey32;
            }
            String secret = props.getFrameIntegritySecret();
            if (secret == null || secret.isBlank()) {
                ZspGatewayLog.diag(LOG, props, Level.SEVERE, "hmac_frame_tag_secret_missing");
                throw new IllegalStateException("zchat.zsp.frame-integrity-secret required for HMAC frame tags");
            }
            hmacKey32 = sha256(secret.getBytes(StandardCharsets.UTF_8));
            return hmacKey32;
        }
    }

    private static byte[] sha256(byte[] in) {
        try {
            return MessageDigest.getInstance("SHA-256").digest(in);
        } catch (NoSuchAlgorithmException e) {
            throw new IllegalStateException(e);
        }
    }

    private static byte[] hmac128(byte[] key32, ZspHeader header, byte[] meta, byte[] payload) {
        try {
            Mac mac = Mac.getInstance("HmacSHA256");
            mac.init(new SecretKeySpec(key32, "HmacSHA256"));
            mac.update(header.toBytesBigEndian());
            mac.update(meta);
            mac.update(payload);
            byte[] full = mac.doFinal();
            return Arrays.copyOf(full, ZspConstants.AUTH_TAG_LENGTH);
        } catch (Exception e) {
            throw new IllegalStateException(e);
        }
    }
}
