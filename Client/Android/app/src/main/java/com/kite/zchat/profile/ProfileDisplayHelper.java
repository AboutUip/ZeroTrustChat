package com.kite.zchat.profile;

import android.content.Context;

import androidx.annotation.Nullable;

import com.kite.zchat.R;

/** 展示层昵称规则：云端无记录或旧用户时，与 32 位账户 ID（hex）一致。 */
public final class ProfileDisplayHelper {

    private ProfileDisplayHelper() {}

    /**
     * @param serverNicknameUtf8 服务端 NMN1，可能为空或 null（旧用户未写入）
     * @param userIdHex32 当前账户 32 位 hex
     */
    public static String effectiveDisplayName(@Nullable String serverNicknameUtf8, String userIdHex32) {
        if (serverNicknameUtf8 != null) {
            String t = serverNicknameUtf8.trim();
            if (!t.isEmpty()) {
                return t;
            }
        }
        return userIdHex32 != null && userIdHex32.length() == 32 ? userIdHex32 : "";
    }

    /** 聊天气泡：无昵称时不展示账户 ID，而用短称呼（与 {@link #effectiveDisplayName} 区分）。 */
    public static String chatBubbleSelfName(Context context, @Nullable String nicknameUtf8) {
        if (nicknameUtf8 != null) {
            String t = nicknameUtf8.trim();
            if (!t.isEmpty()) {
                return t;
            }
        }
        return context.getString(R.string.chat_bubble_self_fallback);
    }

    /** 聊天气泡：对方无昵称时不展示账户 ID。 */
    public static String chatBubblePeerName(Context context, @Nullable String nicknameUtf8) {
        if (nicknameUtf8 != null) {
            String t = nicknameUtf8.trim();
            if (!t.isEmpty()) {
                return t;
            }
        }
        return context.getString(R.string.chat_bubble_peer_fallback);
    }
}
