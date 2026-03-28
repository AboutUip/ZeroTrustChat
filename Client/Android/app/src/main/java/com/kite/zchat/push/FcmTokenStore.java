package com.kite.zchat.push;

import android.content.Context;
import android.content.SharedPreferences;

import androidx.annotation.Nullable;

/**
 * 保存 FCM 设备令牌，供将来上传到业务服务端做离线推送（须服务端对接 Firebase Admin SDK）。
 */
public final class FcmTokenStore {

    private static final String PREF = "zchat_fcm";
    private static final String KEY_TOKEN = "fcm_token";

    private FcmTokenStore() {}

    public static void save(Context context, @Nullable String token) {
        if (token == null || token.isBlank()) {
            return;
        }
        context.getApplicationContext()
                .getSharedPreferences(PREF, Context.MODE_PRIVATE)
                .edit()
                .putString(KEY_TOKEN, token)
                .apply();
    }

    @Nullable
    public static String getToken(Context context) {
        return context.getApplicationContext()
                .getSharedPreferences(PREF, Context.MODE_PRIVATE)
                .getString(KEY_TOKEN, null);
    }
}
