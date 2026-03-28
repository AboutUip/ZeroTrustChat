package com.kite.zchat.chat;

/** 本地广播：会话列表与当前聊天消息刷新。 */
public final class ChatEvents {

    public static final String ACTION_CONVERSATION_LIST_CHANGED = "com.kite.zchat.CONVERSATION_LIST_CHANGED";
    public static final String ACTION_CHAT_MESSAGES_CHANGED = "com.kite.zchat.CHAT_MESSAGES_CHANGED";
    public static final String EXTRA_PEER_HEX = "peer_hex";

    private ChatEvents() {}
}
