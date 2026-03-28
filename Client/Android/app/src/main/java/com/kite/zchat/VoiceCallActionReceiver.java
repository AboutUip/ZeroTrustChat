package com.kite.zchat;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;

import com.kite.zchat.call.ChatCallLogHelper;
import com.kite.zchat.call.VoiceCallBusySender;
import com.kite.zchat.call.VoiceCallCoordinator;
import com.kite.zchat.call.VoiceCallSignalingQueue;
import com.kite.zchat.push.VoiceCallNotificationHelper;

public final class VoiceCallActionReceiver extends BroadcastReceiver {

    public static final String ACTION_DECLINE_INCOMING = "com.kite.zchat.VOICE_DECLINE_INCOMING";
    public static final String ACTION_HANGUP_ONGOING = "com.kite.zchat.VOICE_HANGUP_ONGOING";

    public static final String EXTRA_PEER_HEX = "peer_hex";

    @Override
    public void onReceive(Context context, Intent intent) {
        if (intent == null || intent.getAction() == null) {
            return;
        }
        Context app = context.getApplicationContext();
        String peerHex = intent.getStringExtra(EXTRA_PEER_HEX);
        if (ACTION_DECLINE_INCOMING.equals(intent.getAction())) {
            if (peerHex != null && peerHex.length() == 32) {
                VoiceCallBusySender.sendReject(app, peerHex);
                ChatCallLogHelper.insertLocal(app, peerHex, "rejected_in", false, System.currentTimeMillis());
                VoiceCallSignalingQueue.clearPeer(peerHex);
                VoiceCallNotificationHelper.cancelIncoming(app, peerHex);
                VoiceCallCoordinator.clear();
                VoiceCallActivity.notifyDeclinedFromExternal(peerHex);
            }
            return;
        }
        if (ACTION_HANGUP_ONGOING.equals(intent.getAction())) {
            VoiceCallActivity.tryHangupFromNotification(app);
            return;
        }
    }
}
