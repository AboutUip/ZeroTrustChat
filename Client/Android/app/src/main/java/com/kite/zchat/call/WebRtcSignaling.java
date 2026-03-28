package com.kite.zchat.call;

import androidx.annotation.Nullable;

import org.json.JSONException;
import org.json.JSONObject;

/**
 * WebRTC 信令经 ZSP 明文 TEXT 传输：正文以此前缀开头时不写入聊天库、不计未读。
 *
 * <p>前缀后为 JSON：{@code {"v":1,"t":"offer|answer|ice|bye","sdp":...}} 或 ICE 字段。
 */
public final class WebRtcSignaling {

    public static final String PREFIX = "__ZRTC1__";

    private WebRtcSignaling() {}

    public static boolean isSignaling(@Nullable String textUtf8) {
        return textUtf8 != null && textUtf8.startsWith(PREFIX);
    }

    /** 去掉前缀后的 JSON 文本。 */
    public static String jsonPayload(@Nullable String textUtf8) {
        if (!isSignaling(textUtf8)) {
            return "";
        }
        return textUtf8.substring(PREFIX.length());
    }

    /** 是否为 SDP offer（用于区分 SYNC 历史重放与实时信令）。 */
    public static boolean isOfferMessage(@Nullable String textUtf8) {
        if (!isSignaling(textUtf8)) {
            return false;
        }
        try {
            JSONObject o = new JSONObject(jsonPayload(textUtf8));
            return "offer".equals(o.optString("t"));
        } catch (JSONException e) {
            return false;
        }
    }
}
