package com.kite.zchat.friends;

import android.content.Context;
import android.content.SharedPreferences;
import android.util.Base64;

import androidx.security.crypto.EncryptedSharedPreferences;
import androidx.security.crypto.MasterKey;

import org.bouncycastle.crypto.AsymmetricCipherKeyPair;
import org.bouncycastle.crypto.params.Ed25519PrivateKeyParameters;

import java.io.IOException;
import java.security.GeneralSecurityException;

/**
 * 每账号本地 Ed25519 身份（32 字节种子）；与服务器 UserData EDJ1 公钥对应。
 * 注销账号时由 {@link com.kite.zchat.auth.AuthActions#finishAccountDeletion} 调用 {@link #clear}。
 */
public final class FriendIdentityStore {

    private static final String PREFS = "zchat_friend_identity_encrypted";
    private static final String KEY_USER_HEX = "bound_user_hex";
    private static final String KEY_SEED_B64 = "ed25519_seed_b64";

    private final SharedPreferences prefs;

    public static FriendIdentityStore create(Context context) {
        try {
            return new FriendIdentityStore(context);
        } catch (GeneralSecurityException | IOException e) {
            throw new RuntimeException(e);
        }
    }

    private FriendIdentityStore(Context context) throws GeneralSecurityException, IOException {
        MasterKey masterKey = new MasterKey.Builder(context).setKeyScheme(MasterKey.KeyScheme.AES256_GCM).build();
        this.prefs =
                EncryptedSharedPreferences.create(
                        context,
                        PREFS,
                        masterKey,
                        EncryptedSharedPreferences.PrefKeyEncryptionScheme.AES256_SIV,
                        EncryptedSharedPreferences.PrefValueEncryptionScheme.AES256_GCM);
    }

    /** 返回与当前 userId 绑定的私钥参数；若无或账号变更则生成并持久化新密钥。 */
    public Ed25519PrivateKeyParameters getOrCreatePrivateKey(String userIdHex32) {
        String bound = prefs.getString(KEY_USER_HEX, "");
        String seedB64 = prefs.getString(KEY_SEED_B64, null);
        if (userIdHex32 != null
                && userIdHex32.length() == 32
                && userIdHex32.equals(bound)
                && seedB64 != null
                && !seedB64.isEmpty()) {
            byte[] seed = Base64.decode(seedB64, Base64.NO_WRAP);
            if (seed.length == 32) {
                return new Ed25519PrivateKeyParameters(seed, 0);
            }
        }
        AsymmetricCipherKeyPair pair = FriendSigning.generateKeyPair();
        Ed25519PrivateKeyParameters sk = (Ed25519PrivateKeyParameters) pair.getPrivate();
        byte[] encoded = sk.getEncoded();
        prefs.edit()
                .putString(KEY_USER_HEX, userIdHex32 != null ? userIdHex32 : "")
                .putString(KEY_SEED_B64, Base64.encodeToString(encoded, Base64.NO_WRAP))
                .apply();
        return new Ed25519PrivateKeyParameters(encoded, 0);
    }

    public void clear() {
        prefs.edit().clear().apply();
    }
}
