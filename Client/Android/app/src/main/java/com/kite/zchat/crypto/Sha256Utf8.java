package com.kite.zchat.crypto;

import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;

/** 与注销账号 ZSP 载荷约定：对 UTF-8 口令做 SHA-256，作为 JNI 双确认令牌（各 32 字节）。 */
public final class Sha256Utf8 {

    private Sha256Utf8() {}

    public static byte[] digestPassword(String passwordUtf8) {
        if (passwordUtf8 == null) {
            throw new IllegalArgumentException("password");
        }
        try {
            MessageDigest md = MessageDigest.getInstance("SHA-256");
            return md.digest(passwordUtf8.getBytes(StandardCharsets.UTF_8));
        } catch (NoSuchAlgorithmException e) {
            throw new IllegalStateException(e);
        }
    }
}
