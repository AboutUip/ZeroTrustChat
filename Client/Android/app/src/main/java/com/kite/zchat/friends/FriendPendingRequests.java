package com.kite.zchat.friends;

import android.content.Context;

import com.kite.zchat.zsp.ZspFriendCodec;
import com.kite.zchat.zsp.ZspSessionManager;

/** 拉取当前账号待处理好友申请数量（需已建立 ZSP 会话）。 */
public final class FriendPendingRequests {

    private FriendPendingRequests() {}

    public static int fetchPendingCount(Context context, String host, int port) {
        if (host == null || host.isBlank() || port <= 0) {
            return 0;
        }
        if (!FriendZspHelper.ensureSession(context, host, port)) {
            return 0;
        }
        byte[] raw = ZspSessionManager.get().friendPendingListGet();
        if (raw == null) {
            return 0;
        }
        return ZspFriendCodec.parsePendingRows(raw).size();
    }
}
