package com.kite.zchat.push;

import android.content.Context;

import com.kite.zchat.auth.AuthCredentialStore;
import com.kite.zchat.call.ChatCallLogHelper;
import com.kite.zchat.call.WebRtcSignaling;
import com.kite.zchat.chat.ChatActivePeer;
import com.kite.zchat.chat.PeerImSession;
import com.kite.zchat.conversation.ConversationPlaceholderStore;
import com.kite.zchat.profile.ProfileDisplayHelper;

import com.kite.zchat.R;

import java.nio.charset.StandardCharsets;
import java.util.Arrays;

/**
 * ZSP 在线时收到服务端转发的 TEXT 帧后，在同步完成后弹出本地通知（进程被杀时须依赖 FCM）。
 */
public final class IncomingPushNotifier {

    private IncomingPushNotifier() {}

    public static void onIncomingTextSynced(
            Context app, String host, int port, byte[] textPayload) {
        if (textPayload == null || textPayload.length < 32) {
            return;
        }
        AuthCredentialStore creds = AuthCredentialStore.create(app);
        byte[] self = creds.getUserIdBytes();
        if (self.length != 16) {
            return;
        }
        byte[] im = Arrays.copyOfRange(textPayload, 0, 16);
        byte[] peer = PeerImSession.peerFromSessionId(im, self);
        String peerHex = AuthCredentialStore.bytesToHex(peer);
        if (peerHex.length() != 32) {
            return;
        }
        if (textPayload.length > 32) {
            String text =
                    new String(
                            textPayload,
                            32,
                            textPayload.length - 32,
                            StandardCharsets.UTF_8);
            if (WebRtcSignaling.isSignaling(text) || ChatCallLogHelper.isCallLog(text)) {
                return;
            }
        }
        String active = ChatActivePeer.getActivePeerHex();
        if (active != null && active.equalsIgnoreCase(peerHex)) {
            return;
        }
        if (ConversationPlaceholderStore.isPeerMuted(app, peerHex)) {
            return;
        }
        String storedName = ConversationPlaceholderStore.getPeerDisplayNameStored(app, peerHex);
        String title =
                ProfileDisplayHelper.chatBubblePeerName(app, storedName != null ? storedName : "");
        String preview = ConversationPlaceholderStore.getLastMessagePreview(app, peerHex);
        if (preview.isEmpty()) {
            preview = app.getString(R.string.notification_message_default_preview);
        }
        ZChatNotificationHelper.showChatMessage(app, host, port, peerHex, title, preview);
    }
}
