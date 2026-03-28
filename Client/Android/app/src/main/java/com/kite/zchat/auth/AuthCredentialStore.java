package com.kite.zchat.auth;

import android.content.Context;
import android.content.SharedPreferences;

import androidx.security.crypto.EncryptedSharedPreferences;
import androidx.security.crypto.MasterKey;

import com.kite.zchat.chat.ChatMessageDb;

import java.io.IOException;
import java.security.GeneralSecurityException;
import java.util.Arrays;

/**
 * 持久化本地口令与 16 字节 userId（hex），用于自动登录与 ZSP 本地口令认证。
 */
public final class AuthCredentialStore {

    private static final String PREFS = "zchat_auth_encrypted";
    private static final String KEY_USER_ID_HEX = "user_id_hex";
    private static final String KEY_PASSWORD = "password_utf8";

    private final SharedPreferences prefs;

    public static AuthCredentialStore create(Context context) {
        try {
            return new AuthCredentialStore(context);
        } catch (GeneralSecurityException | IOException e) {
            throw new RuntimeException(e);
        }
    }

    private AuthCredentialStore(Context context) throws GeneralSecurityException, IOException {
        MasterKey masterKey = new MasterKey.Builder(context).setKeyScheme(MasterKey.KeyScheme.AES256_GCM).build();
        this.prefs =
                EncryptedSharedPreferences.create(
                        context,
                        PREFS,
                        masterKey,
                        EncryptedSharedPreferences.PrefKeyEncryptionScheme.AES256_SIV,
                        EncryptedSharedPreferences.PrefValueEncryptionScheme.AES256_GCM);
    }

    public boolean hasCredentials() {
        String uid = prefs.getString(KEY_USER_ID_HEX, null);
        String pw = prefs.getString(KEY_PASSWORD, null);
        return uid != null && !uid.isBlank() && pw != null && !pw.isBlank();
    }

    public String getUserIdHex() {
        String hex = prefs.getString(KEY_USER_ID_HEX, "");
        return hex != null ? hex : "";
    }

    public byte[] getUserIdBytes() {
        return hexToBytes(getUserIdHex());
    }

    public String getPassword() {
        return prefs.getString(KEY_PASSWORD, "");
    }

    /**
     * 若与已保存账号不同，会清空本地聊天消息库，避免切换账号后增量 SYNC 使用错误的 lastMsgId。
     */
    public void saveCredentials(Context context, String userIdHex32, String passwordUtf8) {
        String prev = getUserIdHex();
        prefs.edit().putString(KEY_USER_ID_HEX, userIdHex32).putString(KEY_PASSWORD, passwordUtf8).apply();
        if (prev != null
                && prev.length() == 32
                && userIdHex32 != null
                && userIdHex32.length() == 32
                && !prev.equalsIgnoreCase(userIdHex32)) {
            ChatMessageDb.wipe(context.getApplicationContext());
        }
    }

    public void clear() {
        prefs.edit().clear().apply();
    }

    public static byte[] hexToBytes(String hex) {
        if (hex == null || hex.length() != 32) {
            return new byte[0];
        }
        byte[] out = new byte[16];
        for (int i = 0; i < 16; i++) {
            int hi = Character.digit(hex.charAt(i * 2), 16);
            int lo = Character.digit(hex.charAt(i * 2 + 1), 16);
            if (hi < 0 || lo < 0) {
                return new byte[0];
            }
            out[i] = (byte) ((hi << 4) | lo);
        }
        return out;
    }

    public static String bytesToHex(byte[] raw) {
        if (raw == null || raw.length != 16) {
            return "";
        }
        StringBuilder sb = new StringBuilder(32);
        for (byte b : raw) {
            sb.append(String.format("%02x", b & 0xFF));
        }
        return sb.toString();
    }

    /**
     * 内存清零不可依赖；调用方勿长期保留密码字节数组。
     */
    public static void clearBytes(byte[] buf) {
        if (buf != null) {
            Arrays.fill(buf, (byte) 0);
        }
    }
}
