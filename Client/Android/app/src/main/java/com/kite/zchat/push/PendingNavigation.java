package com.kite.zchat.push;

import android.content.Intent;
import android.os.Bundle;

import androidx.annotation.Nullable;

/**
 * 未登录用户点击通知进入 {@link com.kite.zchat.LoginActivity} 登录成功后，恢复原先要打开的主界面 Intent 参数。
 */
public final class PendingNavigation {

    @Nullable private static Bundle pendingExtras;

    private PendingNavigation() {}

    public static void saveFromIntent(Intent intent) {
        if (intent == null || intent.getExtras() == null) {
            pendingExtras = null;
            return;
        }
        pendingExtras = new Bundle(intent.getExtras());
    }

    @Nullable
    public static Bundle consume() {
        Bundle b = pendingExtras;
        pendingExtras = null;
        return b;
    }
}
