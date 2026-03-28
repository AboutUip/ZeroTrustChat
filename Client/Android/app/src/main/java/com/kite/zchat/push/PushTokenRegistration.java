package com.kite.zchat.push;

import android.content.Context;

import com.google.android.gms.tasks.Task;
import com.google.firebase.messaging.FirebaseMessaging;

import com.kite.zchat.auth.AuthCredentialStore;

/**
 * 在已登录时获取 FCM 令牌并上传；与 {@link ZChatFirebaseMessagingService#onNewToken} 配合。
 */
public final class PushTokenRegistration {

    private PushTokenRegistration() {}

    /** 进入主界面后调用：拉取当前 token 并上传（去重在 {@link FcmTokenUploader}）。 */
    public static void registerIfSignedIn(Context context) {
        Context app = context.getApplicationContext();
        if (!AuthCredentialStore.create(app).hasCredentials()) {
            return;
        }
        Task<String> t = FirebaseMessaging.getInstance().getToken();
        t.addOnCompleteListener(
                task -> {
                    if (!task.isSuccessful()) {
                        return;
                    }
                    String token = task.getResult();
                    if (token == null || token.isBlank()) {
                        return;
                    }
                    FcmTokenStore.save(app, token);
                    FcmTokenUploader.uploadAsync(app, token);
                });
    }

    /** 与 {@link com.kite.zchat.push.ZChatFirebaseMessagingService#onNewToken} 共用。 */
    public static void onTokenAvailable(Context app, String token) {
        FcmTokenStore.save(app, token);
        FcmTokenUploader.uploadAsync(app, token);
    }
}
