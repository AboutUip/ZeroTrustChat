package com.kite.zchat.push;

import android.content.Context;
import android.content.SharedPreferences;

/**
 * 与 {@link com.kite.zchat.push.FriendRequestPollWorker} 同步：记录上次已知的好友申请数量，避免重复通知。
 */
public final class FriendRequestState {

    private static final String PREF = "zchat_friend_req_notify";
    private static final String KEY_LAST_KNOWN = "last_known_pending_count";

    private FriendRequestState() {}

    public static int getLastKnownPendingCount(Context context) {
        return context.getApplicationContext()
                .getSharedPreferences(PREF, Context.MODE_PRIVATE)
                .getInt(KEY_LAST_KNOWN, -1);
    }

    public static void setLastKnownPendingCount(Context context, int count) {
        context.getApplicationContext()
                .getSharedPreferences(PREF, Context.MODE_PRIVATE)
                .edit()
                .putInt(KEY_LAST_KNOWN, count)
                .apply();
    }
}
