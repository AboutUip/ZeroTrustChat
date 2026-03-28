package com.kite.zchat.chat;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;

import org.json.JSONException;
import org.json.JSONObject;

/**
 * 单聊「引用回复」线性格式：{@code __ZREPLY1__}{"ref":"msgId32","prev":"预览"} + "\n" + 正文。
 * 与纯文本兼容：无此前缀则整段为正文。
 */
public final class ChatReplyCodec {

    private static final String PREFIX = "__ZREPLY1__";

    private ChatReplyCodec() {}

    public static final class Parsed {
        /** 气泡内主正文（不含引用元数据）。 */
        public final String body;
        /** 被引用消息的 32 hex messageId；无引用时为 null。 */
        @Nullable public final String refMsgIdHex;
        /** 被引用片段预览；无引用时为 null。 */
        @Nullable public final String refPreview;

        Parsed(String body, @Nullable String refMsgIdHex, @Nullable String refPreview) {
            this.body = body != null ? body : "";
            this.refMsgIdHex = refMsgIdHex;
            this.refPreview = refPreview;
        }

        public boolean hasQuote() {
            return refMsgIdHex != null && refMsgIdHex.length() == 32;
        }
    }

    @NonNull
    public static String encode(
            @NonNull String refMsgIdHex32, @NonNull String refPreview, @NonNull String bodyUtf8) {
        try {
            JSONObject o = new JSONObject();
            o.put("ref", refMsgIdHex32);
            o.put("prev", refPreview);
            return PREFIX + o.toString() + "\n" + bodyUtf8;
        } catch (JSONException e) {
            return bodyUtf8;
        }
    }

    @NonNull
    public static Parsed parse(@Nullable String wire) {
        if (wire == null || wire.isEmpty()) {
            return new Parsed("", null, null);
        }
        if (!wire.startsWith(PREFIX)) {
            return new Parsed(wire, null, null);
        }
        int nl = wire.indexOf('\n', PREFIX.length());
        if (nl < 0) {
            return new Parsed(wire, null, null);
        }
        String json = wire.substring(PREFIX.length(), nl);
        String body = wire.substring(nl + 1);
        try {
            JSONObject o = new JSONObject(json);
            String ref = o.optString("ref", "");
            String prev = o.optString("prev", "");
            return new Parsed(body, ref.length() == 32 ? ref : null, prev);
        } catch (JSONException e) {
            return new Parsed(body, null, null);
        }
    }

    /** 会话列表预览、通知等：取用户可见正文（去掉引用头）。 */
    @NonNull
    public static String stripForPreview(@Nullable String wire) {
        return parse(wire).body;
    }
}
