package com.kite.zchat.push;

import android.content.Context;
import android.content.SharedPreferences;
import android.util.Log;

import com.kite.zchat.R;
import com.kite.zchat.auth.AuthCredentialStore;
import com.kite.zchat.core.ServerConfigStore;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.IOException;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * 将 FCM 设备令牌 POST 到业务服务器，供服务端调用 Firebase Admin 发离线推送。
 *
 * <p>HTTP 约定（与 ZSP 端口独立）：
 *
 * <pre>
 * POST /api/v1/devices/fcm-token
 * Content-Type: application/json; charset=UTF-8
 * 可选: X-ZChat-Push-Key: &lt;与客户端 strings.xml 中 push_register_secret 一致&gt;
 *
 * {"userIdHex":"32位小写hex","fcmToken":"...","platform":"android"}
 * </pre>
 *
 * 成功：HTTP 2xx，空 body 即可。
 */
public final class FcmTokenUploader {

    private static final String TAG = "ZChatFcmUpload";
    private static final String PREF = "zchat_fcm_upload";
    private static final String KEY_LAST_TOKEN = "last_uploaded_token";
    private static final String KEY_LAST_USER = "last_uploaded_user_hex";

    private static final ExecutorService IO = Executors.newSingleThreadExecutor();

    private FcmTokenUploader() {}

    public static void uploadAsync(Context context, String fcmToken) {
        Context app = context.getApplicationContext();
        IO.execute(() -> uploadSync(app, fcmToken));
    }

    static void uploadSync(Context app, String fcmToken) {
        if (fcmToken == null || fcmToken.isBlank()) {
            return;
        }
        AuthCredentialStore creds = AuthCredentialStore.create(app);
        if (!creds.hasCredentials()) {
            return;
        }
        String userHex = creds.getUserIdHex();
        if (userHex == null || userHex.length() != 32) {
            return;
        }
        if (!shouldUpload(app, userHex, fcmToken)) {
            return;
        }
        ServerConfigStore cfg = new ServerConfigStore(app);
        String url = cfg.getPushRegisterUrl();
        if (url == null) {
            Log.d(TAG, "No saved server endpoint; skip FCM token upload");
            return;
        }
        HttpURLConnection conn = null;
        try {
            JSONObject body = new JSONObject();
            try {
                body.put("userIdHex", userHex.toLowerCase());
                body.put("fcmToken", fcmToken);
                body.put("platform", "android");
            } catch (JSONException e) {
                Log.w(TAG, "FCM register JSON build failed", e);
                return;
            }
            byte[] raw = body.toString().getBytes(StandardCharsets.UTF_8);

            conn = (HttpURLConnection) new URL(url).openConnection();
            conn.setConnectTimeout(15_000);
            conn.setReadTimeout(20_000);
            conn.setRequestMethod("POST");
            conn.setDoOutput(true);
            conn.setRequestProperty("Content-Type", "application/json; charset=UTF-8");
            String secret = readPushSecret(app);
            if (secret != null && !secret.isBlank()) {
                conn.setRequestProperty("X-ZChat-Push-Key", secret);
            }
            try (OutputStream os = conn.getOutputStream()) {
                os.write(raw);
            }
            int code = conn.getResponseCode();
            if (code >= 200 && code < 300) {
                markUploaded(app, userHex, fcmToken);
                Log.i(TAG, "FCM token registered: HTTP " + code);
            } else {
                Log.w(TAG, "FCM token register failed: HTTP " + code);
            }
        } catch (IOException e) {
            Log.w(TAG, "FCM token register error: " + e.getMessage());
        } finally {
            if (conn != null) {
                conn.disconnect();
            }
        }
    }

    private static String readPushSecret(Context app) {
        return app.getString(R.string.push_register_secret);
    }

    private static boolean shouldUpload(Context app, String userHex, String fcmToken) {
        SharedPreferences p = app.getSharedPreferences(PREF, Context.MODE_PRIVATE);
        String lastTok = p.getString(KEY_LAST_TOKEN, "");
        String lastUser = p.getString(KEY_LAST_USER, "");
        return !fcmToken.equals(lastTok) || !userHex.equalsIgnoreCase(lastUser);
    }

    private static void markUploaded(Context app, String userHex, String fcmToken) {
        app.getSharedPreferences(PREF, Context.MODE_PRIVATE)
                .edit()
                .putString(KEY_LAST_TOKEN, fcmToken)
                .putString(KEY_LAST_USER, userHex.toLowerCase())
                .apply();
    }

    /** 登出或换账号后应调用，避免误用旧的去重状态。 */
    public static void clearUploadState(Context app) {
        app.getSharedPreferences(PREF, Context.MODE_PRIVATE).edit().clear().apply();
    }
}
