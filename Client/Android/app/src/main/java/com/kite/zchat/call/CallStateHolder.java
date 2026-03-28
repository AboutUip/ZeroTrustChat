package com.kite.zchat.call;

/** 最小化悬浮窗恢复全屏时保留通话方向等状态。 */
public final class CallStateHolder {

    public static volatile boolean outgoingCall = true;
    public static volatile String peerHex = "";

    private CallStateHolder() {}
}
