package com.kite.zchat.chat;

import androidx.annotation.Nullable;

/** 当前正在查看的单聊会话（用于未读计数：前台打开聊天时不计未读）。 */
public final class ChatActivePeer {

    @Nullable private static volatile String activePeerHex32;

    private ChatActivePeer() {}

    public static void setActivePeerHex(@Nullable String peerHex32) {
        activePeerHex32 = peerHex32;
    }

    @Nullable
    public static String getActivePeerHex() {
        return activePeerHex32;
    }
}
