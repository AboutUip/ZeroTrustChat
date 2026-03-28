package com.kite.zchat.friends;

import android.content.Context;

import com.kite.zchat.auth.AuthCredentialStore;
import com.kite.zchat.core.ServerEndpoint;
import com.kite.zchat.zsp.ZspSessionManager;

import org.bouncycastle.crypto.params.Ed25519PrivateKeyParameters;

/** 建立 ZSP 会话并上传 Ed25519 公钥（EDJ1），供好友验签使用。 */
public final class FriendZspHelper {

    private FriendZspHelper() {}

    public static boolean ensureSession(Context context, String host, int port) {
        if (host == null || host.isBlank() || port <= 0) {
            return false;
        }
        AuthCredentialStore creds = AuthCredentialStore.create(context);
        if (!creds.hasCredentials()) {
            return false;
        }
        byte[] uid = creds.getUserIdBytes();
        String pw = creds.getPassword();
        if (uid.length != 16 || pw.isEmpty()) {
            return false;
        }
        ServerEndpoint ep = new ServerEndpoint(host.trim(), port);
        return ZspSessionManager.get().ensureSession(ep, uid, pw);
    }

    /** 将当前设备身份公钥写入服务端；失败时好友相关操作也会被服务端拒绝。 */
    public static boolean publishIdentityEd25519(Context context) {
        AuthCredentialStore creds = AuthCredentialStore.create(context);
        String hex = creds.getUserIdHex();
        if (hex.length() != 32) {
            return false;
        }
        FriendIdentityStore ids = FriendIdentityStore.create(context);
        Ed25519PrivateKeyParameters sk = ids.getOrCreatePrivateKey(hex);
        byte[] pub = FriendSigning.getPublicKeyBytes(sk);
        return ZspSessionManager.get().identityEd25519Publish(pub);
    }
}
