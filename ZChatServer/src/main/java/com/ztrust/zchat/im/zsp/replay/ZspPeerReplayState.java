package com.ztrust.zchat.im.zsp.replay;

import java.util.Arrays;
import java.util.HashSet;

/**
 * 按连接维护序列号、Meta 时间窗与 nonce 防重放（对齐 02-ZSP-Protocol.md 8.4，工程化近似）。
 */
public final class ZspPeerReplayState {

    private static final int NONCE_CACHE_MAX = 1000;

    private long lastProcessedSeq = -1L;
    private final HashSet<NonceKey> recentNonces = new HashSet<>();

    /**
     * @param timestampWindowMinutes &lt;= 0 表示不检查 Meta 时间戳
     */
    public boolean accept(
            long sequence,
            byte[] nonce12,
            long metaTimestampMs,
            long nowMs,
            int timestampWindowMinutes) {
        if (nonce12 == null || nonce12.length != 12) {
            return false;
        }
        if (timestampWindowMinutes > 0) {
            long win = (long) timestampWindowMinutes * 60_000L;
            if (Math.abs(nowMs - metaTimestampMs) > win) {
                return false;
            }
        }
        if (lastProcessedSeq >= 0L) {
            long low = lastProcessedSeq - 500L;
            long high = lastProcessedSeq + 500L;
            if (sequence < low || sequence > high) {
                return false;
            }
            if (sequence <= lastProcessedSeq) {
                return false;
            }
        }
        NonceKey nk = new NonceKey(nonce12);
        if (!recentNonces.add(nk)) {
            return false;
        }
        if (recentNonces.size() > NONCE_CACHE_MAX) {
            recentNonces.clear();
            recentNonces.add(nk);
        }
        lastProcessedSeq = sequence;
        return true;
    }

    private static final class NonceKey {

        private final byte[] n;

        NonceKey(byte[] n) {
            this.n = Arrays.copyOf(n, n.length);
        }

        @Override
        public boolean equals(Object o) {
            if (this == o) {
                return true;
            }
            if (!(o instanceof NonceKey other)) {
                return false;
            }
            return Arrays.equals(n, other.n);
        }

        @Override
        public int hashCode() {
            return Arrays.hashCode(n);
        }
    }
}
