package com.kite.zchat.push;

/** 通知点击后 {@link com.kite.zchat.MainPlaceholderActivity} 解析的深度链接参数。 */
public final class NotificationNavExtras {

    public static final String EXTRA_FROM_NOTIFICATION = "from_push_notification";
    public static final String EXTRA_NAV_TARGET = "nav_target";

    public static final int NAV_NONE = 0;
    /** 打开通信 Tab（可选与 OPEN_COMMUNICATION 同用） */
    public static final int NAV_MAIN_COMMUNICATION = 1;
    /** 打开单聊 */
    public static final int NAV_OPEN_CHAT = 2;
    /** 打开好友申请列表页 */
    public static final int NAV_FRIEND_REQUESTS = 3;
    /** 好友已删除你：进入通讯录 Tab */
    public static final int NAV_FRIEND_REMOVED = 4;

    public static final String EXTRA_PEER_HEX = "peer_hex";
    public static final String EXTRA_PEER_DISPLAY_NAME = "peer_display_name";

    private NotificationNavExtras() {}
}
