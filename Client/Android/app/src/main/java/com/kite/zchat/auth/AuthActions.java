package com.kite.zchat.auth;

import android.content.Context;
import android.content.Intent;

import com.kite.zchat.MainActivity;
import com.kite.zchat.chat.ChatActivePeer;
import com.kite.zchat.chat.ChatMessageDb;
import com.kite.zchat.contacts.ContactsCache;
import com.kite.zchat.friends.FriendIdentityStore;
import com.kite.zchat.profile.LocalProfileStore;
import com.kite.zchat.push.FcmTokenUploader;
import com.kite.zchat.zsp.ZspSessionManager;

/** 退出登录 / 注销后的导航与本地清理。 */
public final class AuthActions {

    private AuthActions() {}

    /** 关闭 ZSP、清除凭据，回到服务器连接/登录入口。 */
    public static void signOut(Context context) {
        ZspSessionManager.get().close();
        ChatActivePeer.setActivePeerHex(null);
        ChatMessageDb.wipe(context.getApplicationContext());
        ContactsCache.clear();
        FcmTokenUploader.clearUploadState(context.getApplicationContext());
        AuthCredentialStore.create(context).clear();
        Intent i = new Intent(context, MainActivity.class);
        i.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
        context.startActivity(i);
    }

    /** 注销成功后：删本机头像缓存、关闭会话、清凭据并回到入口。 */
    public static void finishAccountDeletion(Context context, String userIdHex32) {
        ZspSessionManager.get().close();
        ContactsCache.clear();
        ChatMessageDb.wipe(context.getApplicationContext());
        FriendIdentityStore.create(context).clear();
        if (userIdHex32 != null && userIdHex32.length() == 32) {
            LocalProfileStore.clearAvatar(context, userIdHex32);
        }
        FcmTokenUploader.clearUploadState(context.getApplicationContext());
        AuthCredentialStore.create(context).clear();
        Intent i = new Intent(context, MainActivity.class);
        i.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
        context.startActivity(i);
    }
}
