package com.kite.zchat.call;

import android.content.Context;
import android.content.Intent;
import android.os.Handler;
import android.os.Looper;

import androidx.annotation.Nullable;

import org.json.JSONException;
import org.json.JSONObject;

import com.kite.zchat.auth.AuthCredentialStore;
import com.kite.zchat.chat.PeerImSession;
import com.kite.zchat.VoiceCallActivity;
import com.kite.zchat.conversation.ConversationPlaceholderStore;
import com.kite.zchat.core.ServerConfigStore;
import com.kite.zchat.core.ServerEndpoint;
import com.kite.zchat.profile.ProfileDisplayHelper;
import com.kite.zchat.push.VoiceCallNotificationHelper;

import java.nio.charset.StandardCharsets;
import java.util.Arrays;

/**
 * 分发 WebRTC 信令；来电 offer 拉起界面；忙线时回复 busy；接听前缓存信令。
 */
public final class VoiceCallEngine {

    private static final Object LOCK = new Object();
    private static final Handler MAIN = new Handler(Looper.getMainLooper());

    @Nullable private static WebRtcAudioCallSession activeSession;

    private VoiceCallEngine() {}

    public static void setActiveSession(@Nullable WebRtcAudioCallSession s) {
        synchronized (LOCK) {
            activeSession = s;
        }
    }

    /**
     * ZSP 在线收到 MESSAGE_TEXT 全帧载荷（im16‖to16‖utf8）时调用；须先于 {@link
     * com.kite.zchat.chat.ChatSync#scheduleSyncFromIncomingPush}，否则仅依赖 SYNC 会丢失首包 offer。
     */
    public static void dispatchSignalingFromIncomingTextPayload(Context app, byte[] payload) {
        if (payload == null || payload.length <= 32) {
            return;
        }
        AuthCredentialStore creds = AuthCredentialStore.create(app);
        byte[] self = creds.getUserIdBytes();
        if (self.length != 16) {
            return;
        }
        byte[] im = Arrays.copyOfRange(payload, 0, 16);
        byte[] peer = PeerImSession.peerFromSessionId(im, self);
        String peerHex = AuthCredentialStore.bytesToHex(peer);
        if (peerHex.length() != 32) {
            return;
        }
        String text =
                new String(
                        payload, 32, payload.length - 32, StandardCharsets.UTF_8);
        if (WebRtcSignaling.isSignaling(text)) {
            dispatchSignaling(app, peerHex, text);
        }
    }

    /** 从后台线程调用：将信令交给当前会话、排队或启动来电。 */
    public static void dispatchSignaling(Context app, String peerHex32, String fullText) {
        if (!WebRtcSignaling.isSignaling(fullText)) {
            return;
        }
        String json = WebRtcSignaling.jsonPayload(fullText);
        WebRtcAudioCallSession s;
        synchronized (LOCK) {
            s = activeSession;
        }
        if (s != null && peerHex32.equalsIgnoreCase(s.getPeerHex())) {
            MAIN.post(() -> s.onRemoteJson(json));
            return;
        }

        if (isOfferJson(json)) {
            if (!VoiceCallCoordinator.canAcceptIncomingOffer()) {
                /*
                 * 非 IDLE 包括：正在外呼、来电响铃、已接通。同一 peer 再次收到 offer（glare、SYNC 重传等）
                 * 绝不能回 busy，否则对方会收到 busy 信令并本地显示 BUSY，表现为「一接通就忙线」。
                 */
                if (VoiceCallCoordinator.isSameActivePeer(peerHex32)) {
                    WebRtcAudioCallSession sess;
                    synchronized (LOCK) {
                        sess = activeSession;
                    }
                    if (sess != null) {
                        MAIN.post(() -> sess.onRemoteJson(json));
                        return;
                    }
                    if (VoiceCallCoordinator.isRingingIncomingFrom(peerHex32)) {
                        MAIN.post(
                                () ->
                                        VoiceCallActivity.notifyIncomingOfferUpdated(
                                                peerHex32, json));
                        return;
                    }
                    VoiceCallSignalingQueue.enqueue(peerHex32, json);
                    return;
                }
                VoiceCallBusySender.sendBusy(app, peerHex32);
                return;
            }
            if (VoiceCallCoordinator.isRingingIncomingFrom(peerHex32)) {
                MAIN.post(() -> VoiceCallActivity.notifyIncomingOfferUpdated(peerHex32, json));
                return;
            }
            VoiceCallCoordinator.markIncomingRinging(peerHex32);
            MAIN.post(() -> showIncomingUi(app, peerHex32, json));
            return;
        }

        VoiceCallSignalingQueue.enqueue(peerHex32, json);
    }

    private static void showIncomingUi(Context app, String peerHex32, String offerJson) {
        ServerEndpoint ep = new ServerConfigStore(app).getSavedEndpoint();
        String host = ep != null ? ep.host() : null;
        int port = ep != null ? ep.port() : 0;
        String stored = ConversationPlaceholderStore.getPeerDisplayNameStored(app, peerHex32);
        String name = ProfileDisplayHelper.chatBubblePeerName(app, stored != null ? stored : "");
        VoiceCallNotificationHelper.showIncomingCall(app, peerHex32, name, offerJson, host, port);
        Intent i = VoiceCallActivity.buildIncomingIntent(app, host, port, peerHex32, offerJson, false);
        i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        app.startActivity(i);
    }

    private static boolean isOfferJson(String json) {
        if (json.isEmpty()) {
            return false;
        }
        try {
            JSONObject o = new JSONObject(json);
            return "offer".equals(o.optString("t"));
        } catch (JSONException e) {
            return false;
        }
    }
}
