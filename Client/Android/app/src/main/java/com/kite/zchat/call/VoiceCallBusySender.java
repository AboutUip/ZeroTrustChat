package com.kite.zchat.call;

import android.content.Context;

import com.kite.zchat.auth.AuthCredentialStore;
import com.kite.zchat.chat.PeerImSession;
import com.kite.zchat.core.ServerConfigStore;
import com.kite.zchat.core.ServerEndpoint;
import com.kite.zchat.friends.FriendZspHelper;
import com.kite.zchat.zsp.ZspChatWire;
import com.kite.zchat.zsp.ZspSessionManager;

import org.json.JSONException;
import org.json.JSONObject;

import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/** 向对方发送忙线 / 拒接 信令（经 {@link WebRtcSignaling#PREFIX}）。 */
public final class VoiceCallBusySender {

    private static final ExecutorService IO = Executors.newSingleThreadExecutor();

    private VoiceCallBusySender() {}

    public static void sendBusy(Context context, String peerHex32) {
        sendSimple(context, peerHex32, "busy");
    }

    public static void sendReject(Context context, String peerHex32) {
        sendSimple(context, peerHex32, "reject");
    }

    private static void sendSimple(Context app, String peerHex32, String type) {
        IO.execute(
                () -> {
                    ServerEndpoint ep = new ServerConfigStore(app).getSavedEndpoint();
                    if (ep == null) {
                        return;
                    }
                    if (!FriendZspHelper.ensureSession(app, ep.host(), ep.port())) {
                        return;
                    }
                    AuthCredentialStore creds = AuthCredentialStore.create(app);
                    byte[] self = creds.getUserIdBytes();
                    byte[] peer = AuthCredentialStore.hexToBytes(peerHex32);
                    if (self.length != 16 || peer.length != 16) {
                        return;
                    }
                    byte[] im = PeerImSession.deriveSessionId(self, peer);
                    try {
                        JSONObject o = new JSONObject();
                        o.put("v", 1);
                        o.put("t", type);
                        String json = o.toString();
                        String text = WebRtcSignaling.PREFIX + json;
                        ZspSessionManager.get().sendTextMessage(im, peer, text);
                    } catch (JSONException ignored) {
                    }
                });
    }
}
