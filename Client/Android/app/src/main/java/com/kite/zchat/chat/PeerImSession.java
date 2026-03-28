package com.kite.zchat.chat;

import java.util.Arrays;

/**
 * 单聊 IM 会话键：与对端 userId 按字节异或，双方计算结果一致，便于 MM2 按会话拉取。
 */
public final class PeerImSession {

    private PeerImSession() {}

    public static byte[] deriveSessionId(byte[] selfUserId16, byte[] peerUserId16) {
        if (selfUserId16 == null
                || peerUserId16 == null
                || selfUserId16.length != 16
                || peerUserId16.length != 16) {
            return new byte[0];
        }
        byte[] out = new byte[16];
        for (int i = 0; i < 16; i++) {
            out[i] = (byte) (selfUserId16[i] ^ peerUserId16[i]);
        }
        return out;
    }

    /** 已知会话键与本人 userId 时还原对端 userId（与 deriveSessionId 互逆）。 */
    public static byte[] peerFromSessionId(byte[] sessionId16, byte[] selfUserId16) {
        return deriveSessionId(sessionId16, selfUserId16);
    }

    public static boolean equalBytes(byte[] a, byte[] b) {
        return a != null && b != null && a.length == b.length && Arrays.equals(a, b);
    }
}
