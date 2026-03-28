package com.kite.zchat.call;

import android.content.Context;
import android.content.Intent;

import com.kite.zchat.auth.AuthCredentialStore;
import com.kite.zchat.chat.ChatEvents;
import com.kite.zchat.chat.ChatMessageDb;
import com.kite.zchat.conversation.ConversationPlaceholderStore;

import org.json.JSONException;
import org.json.JSONObject;

import java.security.SecureRandom;

/**
 * 本地通话记录（不回传服务器）：正文前缀 {@link #PREFIX} + JSON。
 *
 * <p>事件 e：out_dial, in_ring, answered, rejected_out, rejected_in, busy_peer, ended, missed
 */
public final class ChatCallLogHelper {

    public static final String PREFIX = "__ZCALL1__";

    private ChatCallLogHelper() {}

    public static boolean isCallLog(@androidx.annotation.Nullable String textUtf8) {
        return textUtf8 != null && textUtf8.startsWith(PREFIX);
    }

    public static String jsonPayload(String textUtf8) {
        if (!isCallLog(textUtf8)) {
            return "";
        }
        return textUtf8.substring(PREFIX.length());
    }

    public static void insertLocal(
            Context context, String peerHex32, String event, boolean outgoing, long tsMs) {
        insertLocal(context, peerHex32, event, outgoing, tsMs, 0, true);
    }

    /** @param durationSec 仅 ended 时有意义 */
    public static void insertLocal(
            Context context,
            String peerHex32,
            String event,
            boolean outgoing,
            long tsMs,
            int durationSec) {
        insertLocal(context, peerHex32, event, outgoing, tsMs, durationSec, true);
    }

    /**
     * @param updateConversationPreview 为 false 时仍写入聊天并刷新消息列表，但不更新通信会话预览（用于 out_dial
     *     避免列表出现「正在呼叫」；接通后由界面另行更新预览）。
     */
    public static void insertLocal(
            Context context,
            String peerHex32,
            String event,
            boolean outgoing,
            long tsMs,
            int durationSec,
            boolean updateConversationPreview) {
        if (peerHex32 == null || peerHex32.length() != 32) {
            return;
        }
        Context app = context.getApplicationContext();
        try {
            JSONObject o = new JSONObject();
            o.put("e", event);
            if (durationSec > 0) {
                o.put("d", durationSec);
            }
            String text = PREFIX + o.toString();
            byte[] id = new byte[16];
            new SecureRandom().nextBytes(id);
            String msgId = AuthCredentialStore.bytesToHex(id);
            ChatMessageDb db = new ChatMessageDb(app);
            if (db.insertIfAbsent(peerHex32, msgId, text, outgoing, tsMs)) {
                if (updateConversationPreview) {
                    ConversationPlaceholderStore.updatePreviewAndTime(
                            app, peerHex32, previewForEvent(app, event, durationSec), tsMs);
                }
                broadcast(app, peerHex32);
            }
        } catch (JSONException ignored) {
        }
    }

    private static String previewForEvent(Context app, String event, int durationSec) {
        int res = previewStringRes(event);
        if (res == 0) {
            return app.getString(com.kite.zchat.R.string.chat_call_preview_default);
        }
        if ("ended".equals(event) && durationSec > 0) {
            return app.getString(com.kite.zchat.R.string.chat_call_preview_ended_d, durationSec);
        }
        return app.getString(res);
    }

    private static int previewStringRes(String event) {
        switch (event) {
            case "out_dial":
                return com.kite.zchat.R.string.chat_call_preview_out_dial;
            case "in_ring":
                return com.kite.zchat.R.string.chat_call_preview_in_ring;
            case "answered":
                return com.kite.zchat.R.string.chat_call_preview_answered;
            case "rejected_out":
                return com.kite.zchat.R.string.chat_call_preview_rejected_out;
            case "rejected_in":
                return com.kite.zchat.R.string.chat_call_preview_rejected_in;
            case "busy_peer":
                return com.kite.zchat.R.string.chat_call_preview_busy_peer;
            case "ended":
                return com.kite.zchat.R.string.chat_call_preview_ended;
            case "missed":
                return com.kite.zchat.R.string.chat_call_preview_missed;
            default:
                return com.kite.zchat.R.string.chat_call_preview_default;
        }
    }

    /** 会话列表中展示的短文案（与气泡文案可不同）。 */
    public static String formatBubbleLine(Context app, String jsonPayload) {
        if (jsonPayload.isEmpty()) {
            return "";
        }
        try {
            JSONObject o = new JSONObject(jsonPayload);
            String e = o.optString("e");
            int d = o.optInt("d", 0);
            switch (e) {
                case "out_dial":
                    return app.getString(com.kite.zchat.R.string.chat_call_line_out_dial);
                case "in_ring":
                    return app.getString(com.kite.zchat.R.string.chat_call_line_in_ring);
                case "answered":
                    return app.getString(com.kite.zchat.R.string.chat_call_line_answered);
                case "rejected_out":
                    return app.getString(com.kite.zchat.R.string.chat_call_line_rejected_out);
                case "rejected_in":
                    return app.getString(com.kite.zchat.R.string.chat_call_line_rejected_in);
                case "busy_peer":
                    return app.getString(com.kite.zchat.R.string.chat_call_line_busy_peer);
                case "ended":
                    if (d > 0) {
                        return app.getString(com.kite.zchat.R.string.chat_call_line_ended_d, d);
                    }
                    return app.getString(com.kite.zchat.R.string.chat_call_line_ended);
                case "missed":
                    return app.getString(com.kite.zchat.R.string.chat_call_line_missed);
                default:
                    return app.getString(com.kite.zchat.R.string.chat_call_line_default);
            }
        } catch (JSONException e) {
            return "";
        }
    }

    private static void broadcast(Context app, String peerHex) {
        Intent i = new Intent(ChatEvents.ACTION_CONVERSATION_LIST_CHANGED);
        i.setPackage(app.getPackageName());
        app.sendBroadcast(i);
        Intent j = new Intent(ChatEvents.ACTION_CHAT_MESSAGES_CHANGED);
        j.setPackage(app.getPackageName());
        j.putExtra(ChatEvents.EXTRA_PEER_HEX, peerHex);
        app.sendBroadcast(j);
        String active = com.kite.zchat.chat.ChatActivePeer.getActivePeerHex();
        if (active != null && active.equalsIgnoreCase(peerHex)) {
            com.kite.zchat.ChatActivity.notifyMessagesChangedIfShowing(peerHex);
        }
    }
}
