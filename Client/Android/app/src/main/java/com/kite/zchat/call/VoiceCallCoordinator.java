package com.kite.zchat.call;

import androidx.annotation.Nullable;

/**
 * 全局语音通话相斥：未空闲时拒绝其他用户的 offer（忙线），并阻止向他人拨号。
 */
public final class VoiceCallCoordinator {

    public enum Phase {
        IDLE,
        /** 正在向外发起 */
        OUTGOING,
        /** 来电响铃（未接听） */
        RINGING_INCOMING,
        /** 已建立媒体或已接听正在协商 */
        CONNECTED
    }

    private static final Object LOCK = new Object();

    private static Phase phase = Phase.IDLE;
    @Nullable private static String activePeerHex;

    private VoiceCallCoordinator() {}

    public static Phase getPhase() {
        synchronized (LOCK) {
            return phase;
        }
    }

    @Nullable
    public static String getActivePeerHex() {
        synchronized (LOCK) {
            return activePeerHex;
        }
    }

    /** 仅空闲时可向他人发起新通话。 */
    public static boolean canStartOutgoingCall() {
        synchronized (LOCK) {
            return phase == Phase.IDLE;
        }
    }

    /** 仅空闲时可接受新的来电 offer。 */
    public static boolean canAcceptIncomingOffer() {
        synchronized (LOCK) {
            return phase == Phase.IDLE;
        }
    }

    public static boolean isSameActivePeer(@Nullable String peerHex32) {
        if (peerHex32 == null || peerHex32.length() != 32) {
            return false;
        }
        synchronized (LOCK) {
            return activePeerHex != null && activePeerHex.equalsIgnoreCase(peerHex32);
        }
    }

    public static boolean isRingingIncomingFrom(@Nullable String peerHex32) {
        synchronized (LOCK) {
            return phase == Phase.RINGING_INCOMING
                    && peerHex32 != null
                    && activePeerHex != null
                    && activePeerHex.equalsIgnoreCase(peerHex32);
        }
    }

    public static void markOutgoingStarted(String peerHex32) {
        synchronized (LOCK) {
            phase = Phase.OUTGOING;
            activePeerHex = peerHex32;
        }
    }

    public static void markIncomingRinging(String peerHex32) {
        synchronized (LOCK) {
            phase = Phase.RINGING_INCOMING;
            activePeerHex = peerHex32;
        }
    }

    public static void markConnected(String peerHex32) {
        synchronized (LOCK) {
            phase = Phase.CONNECTED;
            activePeerHex = peerHex32;
        }
    }

    public static void clear() {
        synchronized (LOCK) {
            phase = Phase.IDLE;
            activePeerHex = null;
        }
    }
}
