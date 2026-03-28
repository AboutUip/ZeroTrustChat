package com.kite.zchat;

import android.Manifest;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.media.AudioManager;
import android.os.Build;
import android.os.Bundle;
import android.os.SystemClock;
import android.provider.Settings;
import android.view.View;
import android.widget.Chronometer;
import android.widget.ImageView;
import android.widget.TextView;
import android.widget.Toast;

import androidx.activity.OnBackPressedCallback;
import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.annotation.Nullable;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.content.ContextCompat;

import com.google.android.material.appbar.MaterialToolbar;
import com.google.android.material.button.MaterialButton;

import com.kite.zchat.auth.AuthCredentialStore;
import com.kite.zchat.call.CallStateHolder;
import com.kite.zchat.call.ChatCallLogHelper;
import com.kite.zchat.call.VoiceCallCoordinator;
import com.kite.zchat.call.VoiceCallEngine;
import com.kite.zchat.call.VoiceCallSignalingQueue;
import com.kite.zchat.call.VoiceCallBusySender;
import com.kite.zchat.call.WebRtcAudioCallSession;
import com.kite.zchat.call.WebRtcSignaling;
import com.kite.zchat.chat.ChatEvents;
import com.kite.zchat.chat.PeerImSession;
import com.kite.zchat.conversation.ConversationPlaceholderStore;
import com.kite.zchat.core.ServerConfigStore;
import com.kite.zchat.core.ServerEndpoint;
import com.kite.zchat.friends.FriendZspHelper;
import com.kite.zchat.profile.ProfileDisplayHelper;
import com.kite.zchat.push.VoiceCallNotificationHelper;
import com.kite.zchat.zsp.ZspChatWire;
import com.kite.zchat.zsp.ZspSessionManager;

import java.io.File;
import java.lang.ref.WeakReference;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public final class VoiceCallActivity extends AppCompatActivity {

    public static final String EXTRA_HOST = "voice_host";
    public static final String EXTRA_PORT = "voice_port";
    public static final String EXTRA_PEER_HEX = "voice_peer_hex";
    public static final String EXTRA_PEER_DISPLAY_NAME = "voice_peer_display_name";
    public static final String EXTRA_INCOMING_JSON = "voice_incoming_json";
    public static final String EXTRA_AUTO_ANSWER = "voice_auto_answer";
    public static final String EXTRA_RESUME_FULL = "voice_resume_full";

    @Nullable private static WeakReference<VoiceCallActivity> activeRef;

    private String host;
    private int port;
    private String peerHex;
    private String peerDisplayName = "";
    private boolean isOutgoingCall;
    private final ExecutorService io = Executors.newSingleThreadExecutor();

    @Nullable private WebRtcAudioCallSession session;
    private TextView statusView;
    private TextView peerNameView;
    private ImageView avatarView;
    private Chronometer chronometer;
    private View ringActions;
    private View inCallActions;
    private MaterialButton muteBtn;
    private MaterialButton speakerBtn;
    private boolean muted;
    private boolean speakerOn;
    private boolean ringMode;
    private boolean autoAnswerPending;
    private String pendingOfferJson;
    private long connectBaseElapsed;
    private boolean chronometerStarted;
    private boolean callListPreviewUpdatedForSession;
    @Nullable private AudioManager audioManager;

    private final ActivityResultLauncher<String> permissionLauncher =
            registerForActivityResult(
                    new ActivityResultContracts.RequestPermission(),
                    granted -> {
                        if (granted) {
                            afterMicGranted();
                        } else {
                            Toast.makeText(this, R.string.voice_call_need_mic, Toast.LENGTH_SHORT)
                                    .show();
                            finishWithCleanup();
                        }
                    });

    public static Intent buildOutgoingIntent(
            android.content.Context c, String host, int port, String peerHex32) {
        Intent i = new Intent(c, VoiceCallActivity.class);
        i.putExtra(EXTRA_HOST, host);
        i.putExtra(EXTRA_PORT, port);
        i.putExtra(EXTRA_PEER_HEX, peerHex32);
        return i;
    }

    public static Intent buildIncomingIntent(
            android.content.Context c,
            @Nullable String host,
            int port,
            String peerHex32,
            String offerJson,
            boolean autoAnswer) {
        Intent i = new Intent(c, VoiceCallActivity.class);
        if (host != null) {
            i.putExtra(EXTRA_HOST, host);
        }
        i.putExtra(EXTRA_PORT, port);
        i.putExtra(EXTRA_PEER_HEX, peerHex32);
        i.putExtra(EXTRA_INCOMING_JSON, offerJson);
        i.putExtra(EXTRA_AUTO_ANSWER, autoAnswer);
        return i;
    }

    public static Intent buildResumeIntent(Context c, String peerHex32, String peerDisplayName) {
        Intent i = new Intent(c, VoiceCallActivity.class);
        i.putExtra(EXTRA_RESUME_FULL, true);
        i.putExtra(EXTRA_PEER_HEX, peerHex32);
        i.putExtra(EXTRA_PEER_DISPLAY_NAME, peerDisplayName != null ? peerDisplayName : "");
        return i;
    }

    public static void tryHangupFromNotification(Context app) {
        VoiceCallActivity a = activeRef != null ? activeRef.get() : null;
        if (a != null) {
            a.runOnUiThread(
                    () -> {
                        if (a.session != null) {
                            a.hangupAndFinish();
                        } else {
                            a.finishWithCleanup();
                        }
                    });
        } else {
            VoiceCallForegroundService.stop(app);
            VoiceCallCoordinator.clear();
        }
    }

    public static void notifyDeclinedFromExternal(String peerHex) {
        VoiceCallActivity act = activeRef != null ? activeRef.get() : null;
        if (act != null
                && peerHex != null
                && peerHex.equalsIgnoreCase(act.peerHex)
                && act.ringMode) {
            act.runOnUiThread(act::finishWithCleanup);
        }
    }

    public static void notifyIncomingOfferUpdated(String peerHex32, String fullOfferJson) {
        VoiceCallActivity a = activeRef != null ? activeRef.get() : null;
        if (a != null
                && peerHex32 != null
                && peerHex32.equalsIgnoreCase(a.peerHex)
                && a.ringMode) {
            a.runOnUiThread(() -> a.pendingOfferJson = fullOfferJson);
        }
    }

    @Override
    protected void onCreate(@Nullable Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getOnBackPressedDispatcher()
                .addCallback(
                        this,
                        new OnBackPressedCallback(true) {
                            @Override
                            public void handleOnBackPressed() {
                                hangupOrMinimize();
                            }
                        });
        setContentView(R.layout.activity_voice_call);
        activeRef = new WeakReference<>(this);
        audioManager = (AudioManager) getSystemService(AUDIO_SERVICE);

        if (!parseIntent()) {
            Toast.makeText(this, R.string.voice_call_intent_invalid, Toast.LENGTH_SHORT).show();
            finish();
            return;
        }
        resolveServerEndpoint();
        if (host == null || host.isBlank() || port <= 0) {
            Toast.makeText(this, R.string.profile_sync_no_server, Toast.LENGTH_SHORT).show();
            finish();
            return;
        }

        CallStateHolder.peerHex = peerHex;
        CallStateHolder.outgoingCall = isOutgoingCall;

        MaterialToolbar toolbar = findViewById(R.id.voiceCallToolbar);
        toolbar.setNavigationOnClickListener(v -> hangupOrMinimize());
        toolbar.setTitle(R.string.voice_call_title);

        peerNameView = findViewById(R.id.voiceCallPeerName);
        avatarView = findViewById(R.id.voiceCallAvatar);
        chronometer = findViewById(R.id.voiceCallChronometer);
        statusView = findViewById(R.id.voiceCallStatus);
        ringActions = findViewById(R.id.voiceCallRingActions);
        inCallActions = findViewById(R.id.voiceCallInCallActions);
        MaterialButton answer = findViewById(R.id.voiceCallAnswer);
        MaterialButton reject = findViewById(R.id.voiceCallReject);
        muteBtn = findViewById(R.id.voiceCallMute);
        speakerBtn = findViewById(R.id.voiceCallSpeaker);
        MaterialButton minimizeBtn = findViewById(R.id.voiceCallMinimize);
        MaterialButton hangup = findViewById(R.id.voiceCallHangup);

        loadPeerUi();

        answer.setOnClickListener(v -> onAnswerClicked());
        reject.setOnClickListener(v -> onRejectClicked());
        muteBtn.setOnClickListener(
                v -> {
                    muted = !muted;
                    muteBtn.setText(muted ? R.string.voice_call_unmute : R.string.voice_call_mute);
                    if (session != null) {
                        session.setMuted(muted);
                    }
                });
        speakerBtn.setOnClickListener(v -> toggleSpeaker());
        minimizeBtn.setOnClickListener(v -> minimizeToBubble());
        hangup.setOnClickListener(v -> hangupAndFinish());

        if (ringMode) {
            ringActions.setVisibility(View.VISIBLE);
            inCallActions.setVisibility(View.GONE);
            chronometer.setVisibility(View.GONE);
            statusView.setText(R.string.voice_call_status_incoming);
            VoiceCallNotificationHelper.cancelIncoming(this, peerHex);
        } else {
            ringActions.setVisibility(View.GONE);
            inCallActions.setVisibility(View.VISIBLE);
            VoiceCallCoordinator.markOutgoingStarted(peerHex);
            ChatCallLogHelper.insertLocal(
                    this,
                    peerHex,
                    "out_dial",
                    true,
                    System.currentTimeMillis(),
                    0,
                    false);
        }

        if (ContextCompat.checkSelfPermission(this, Manifest.permission.RECORD_AUDIO)
                != PackageManager.PERMISSION_GRANTED) {
            permissionLauncher.launch(Manifest.permission.RECORD_AUDIO);
        } else {
            afterMicGranted();
        }
    }

    private boolean parseIntent() {
        Intent i = getIntent();
        if (i.getBooleanExtra(EXTRA_RESUME_FULL, false)) {
            peerHex = i.getStringExtra(EXTRA_PEER_HEX);
            peerDisplayName = i.getStringExtra(EXTRA_PEER_DISPLAY_NAME);
            if (peerDisplayName == null) {
                peerDisplayName = "";
            }
            if (peerHex == null || peerHex.length() != 32) {
                return false;
            }
            peerHex = peerHex.trim().toLowerCase(Locale.ROOT);
            isOutgoingCall = CallStateHolder.outgoingCall;
            ringMode = false;
            pendingOfferJson = null;
            return true;
        }
        peerHex = i.getStringExtra(EXTRA_PEER_HEX);
        if (peerHex == null || peerHex.length() != 32) {
            return false;
        }
        peerHex = peerHex.trim().toLowerCase(Locale.ROOT);
        host = i.getStringExtra(EXTRA_HOST);
        port = i.getIntExtra(EXTRA_PORT, 0);
        peerDisplayName = i.getStringExtra(EXTRA_PEER_DISPLAY_NAME);
        if (peerDisplayName == null) {
            peerDisplayName = "";
        }
        pendingOfferJson = i.getStringExtra(EXTRA_INCOMING_JSON);
        autoAnswerPending = i.getBooleanExtra(EXTRA_AUTO_ANSWER, false);
        ringMode = pendingOfferJson != null && !pendingOfferJson.isEmpty();
        isOutgoingCall = !ringMode;
        return true;
    }

    private void resolveServerEndpoint() {
        if (host == null || host.isBlank() || port <= 0) {
            ServerEndpoint ep = new ServerConfigStore(this).getSavedEndpoint();
            if (ep != null) {
                host = ep.host();
                port = ep.port();
            }
        }
    }

    private void loadPeerUi() {
        String stored = ConversationPlaceholderStore.getPeerDisplayNameStored(this, peerHex);
        String merged =
                peerDisplayName != null && !peerDisplayName.isBlank()
                        ? peerDisplayName
                        : (stored != null ? stored : "");
        peerNameView.setText(ProfileDisplayHelper.chatBubblePeerName(this, merged));
        File avatar = ConversationPlaceholderStore.avatarFile(this, peerHex);
        if (avatar != null && avatar.isFile() && avatar.length() > 0) {
            Bitmap bmp = BitmapFactory.decodeFile(avatar.getAbsolutePath());
            if (bmp != null) {
                avatarView.setImageBitmap(bmp);
                return;
            }
        }
        avatarView.setImageResource(R.drawable.ic_avatar_default);
    }

    private void afterMicGranted() {
        if (ringMode) {
            if (autoAnswerPending) {
                onAnswerClicked();
            }
            return;
        }
        startCallFlowOutgoing();
    }

    private void onAnswerClicked() {
        if (!ringMode || session != null) {
            return;
        }
        VoiceCallNotificationHelper.cancelIncoming(this, peerHex);
        ringActions.setVisibility(View.GONE);
        inCallActions.setVisibility(View.VISIBLE);
        ringMode = false;
        VoiceCallCoordinator.markConnected(peerHex);
        ChatCallLogHelper.insertLocal(this, peerHex, "answered", false, System.currentTimeMillis());
        startCallFlowIncomingCallee();
    }

    private void onRejectClicked() {
        VoiceCallBusySender.sendReject(this, peerHex);
        ChatCallLogHelper.insertLocal(this, peerHex, "rejected_in", false, System.currentTimeMillis());
        VoiceCallSignalingQueue.clearPeer(peerHex);
        VoiceCallNotificationHelper.cancelIncoming(this, peerHex);
        finishWithCleanup();
    }

    private void hangupOrMinimize() {
        if (ringMode) {
            onRejectClicked();
        } else {
            minimizeToBubble();
        }
    }

    private void toggleSpeaker() {
        speakerOn = !speakerOn;
        speakerBtn.setText(
                speakerOn ? R.string.voice_call_earpiece : R.string.voice_call_speaker);
        if (audioManager != null) {
            audioManager.setSpeakerphoneOn(speakerOn);
        }
    }

    private void minimizeToBubble() {
        if (ringMode) {
            Toast.makeText(this, R.string.voice_call_minimize_after_connect, Toast.LENGTH_SHORT)
                    .show();
            return;
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M
                && !Settings.canDrawOverlays(this)) {
            startActivity(
                    new Intent(
                            Settings.ACTION_MANAGE_OVERLAY_PERMISSION,
                            android.net.Uri.parse("package:" + getPackageName())));
            Toast.makeText(this, R.string.voice_call_overlay_hint, Toast.LENGTH_SHORT).show();
        }
        CallStateHolder.outgoingCall = isOutgoingCall;
        CallStateHolder.peerHex = peerHex;
        VoiceCallForegroundService.startOrUpdate(
                this,
                peerHex,
                peerDisplayName,
                VoiceCallNotificationHelper.canDrawOverlay(this),
                elapsedMs());
        moveTaskToBack(true);
    }

    private long elapsedMs() {
        if (!chronometerStarted) {
            return 0L;
        }
        return SystemClock.elapsedRealtime() - connectBaseElapsed;
    }

    private void startCallFlowOutgoing() {
        WebRtcAudioCallSession s = buildSession();
        session = s;
        VoiceCallEngine.setActiveSession(s);
        s.startCaller();
    }

    private void startCallFlowIncomingCallee() {
        WebRtcAudioCallSession s = buildSession();
        session = s;
        VoiceCallEngine.setActiveSession(s);
        try {
            org.json.JSONObject o = new org.json.JSONObject(pendingOfferJson);
            if ("offer".equals(o.optString("t"))) {
                s.startCalleeWithOffer(o.optString("sdp"));
            } else {
                s.onRemoteJson(pendingOfferJson);
            }
        } catch (org.json.JSONException e) {
            Toast.makeText(this, R.string.voice_call_bad_signal, Toast.LENGTH_SHORT).show();
            finishWithCleanup();
            return;
        }
        VoiceCallSignalingQueue.drainToSession(peerHex, s);
    }

    private WebRtcAudioCallSession buildSession() {
        if (audioManager != null) {
            audioManager.setMode(AudioManager.MODE_IN_COMMUNICATION);
            audioManager.setSpeakerphoneOn(speakerOn);
        }
        return new WebRtcAudioCallSession(
                this,
                peerHex,
                this::sendSignalingLine,
                new WebRtcAudioCallSession.Listener() {
                    @Override
                    public void onStatus(String line) {
                        runOnUiThread(
                                () -> {
                                    statusView.setText(line);
                                    if (!chronometerStarted
                                            && (line.contains("通话中")
                                                    || line.contains("已连接"))) {
                                        chronometerStarted = true;
                                        connectBaseElapsed = SystemClock.elapsedRealtime();
                                        chronometer.setBase(connectBaseElapsed);
                                        chronometer.setVisibility(View.VISIBLE);
                                        chronometer.start();
                                        VoiceCallCoordinator.markConnected(peerHex);
                                        syncConversationListPreviewOnConnected();
                                        VoiceCallForegroundService.startOrUpdate(
                                                VoiceCallActivity.this,
                                                peerHex,
                                                peerDisplayName,
                                                false,
                                                0L);
                                    }
                                });
                    }

                    @Override
                    public void onGlareAsCallee(String remoteOfferSdp) {
                        runOnUiThread(() -> replaceSessionAsCalleeAfterGlare(remoteOfferSdp));
                    }

                    @Override
                    public void onError(String message) {
                        runOnUiThread(
                                () -> {
                                    Toast.makeText(
                                                    VoiceCallActivity.this,
                                                    message,
                                                    Toast.LENGTH_SHORT)
                                            .show();
                                    if ("BUSY".equals(message)) {
                                        ChatCallLogHelper.insertLocal(
                                                VoiceCallActivity.this,
                                                peerHex,
                                                "busy_peer",
                                                true,
                                                System.currentTimeMillis());
                                    } else if ("REJECT".equals(message)) {
                                        ChatCallLogHelper.insertLocal(
                                                VoiceCallActivity.this,
                                                peerHex,
                                                "rejected_out",
                                                true,
                                                System.currentTimeMillis());
                                    }
                                    finishWithCleanup();
                                });
                    }

                    @Override
                    public void onEnded() {
                        runOnUiThread(
                                () -> {
                                    int sec =
                                            chronometerStarted
                                                    ? (int) (elapsedMs() / 1000L)
                                                    : 0;
                                    ChatCallLogHelper.insertLocal(
                                            VoiceCallActivity.this,
                                            peerHex,
                                            "ended",
                                            isOutgoingCall,
                                            System.currentTimeMillis(),
                                            sec);
                                    finishWithCleanup();
                                });
                    }
                });
    }

    /** out_dial 未写会话预览时，在真正接通后补一条列表预览，避免通信页长期显示「正在呼叫」。 */
    private void syncConversationListPreviewOnConnected() {
        if (callListPreviewUpdatedForSession) {
            return;
        }
        callListPreviewUpdatedForSession = true;
        ConversationPlaceholderStore.updatePreviewAndTime(
                this,
                peerHex,
                getString(R.string.chat_call_preview_answered),
                System.currentTimeMillis());
        Intent i = new Intent(ChatEvents.ACTION_CONVERSATION_LIST_CHANGED);
        i.setPackage(getPackageName());
        sendBroadcast(i);
    }

    private void replaceSessionAsCalleeAfterGlare(String remoteOfferSdp) {
        if (isFinishing()) {
            return;
        }
        if (session != null) {
            session.disposeSilently();
            session = null;
        }
        VoiceCallEngine.setActiveSession(null);
        WebRtcAudioCallSession s = buildSession();
        session = s;
        VoiceCallEngine.setActiveSession(s);
        s.startCalleeWithOffer(remoteOfferSdp);
        VoiceCallSignalingQueue.drainToSession(peerHex, s);
    }

    private boolean sendSignalingLine(String json) {
        if (host == null || host.isBlank() || port <= 0) {
            return false;
        }
        if (!FriendZspHelper.ensureSession(this, host, port)) {
            return false;
        }
        AuthCredentialStore creds = AuthCredentialStore.create(this);
        byte[] self = creds.getUserIdBytes();
        byte[] peer = AuthCredentialStore.hexToBytes(peerHex);
        if (self.length != 16 || peer.length != 16) {
            return false;
        }
        byte[] im = PeerImSession.deriveSessionId(self, peer);
        String text = WebRtcSignaling.PREFIX + json;
        ZspChatWire.TextSendResult r = ZspSessionManager.get().sendTextMessage(im, peer, text);
        return r != null && r.ok;
    }

    private void hangupAndFinish() {
        if (session != null) {
            session.hangup();
        } else {
            finishWithCleanup();
        }
    }

    private void finishWithCleanup() {
        if (chronometer != null && chronometerStarted) {
            chronometer.stop();
        }
        VoiceCallForegroundService.stop(this);
        if (audioManager != null) {
            audioManager.setMode(AudioManager.MODE_NORMAL);
            audioManager.setSpeakerphoneOn(false);
        }
        finish();
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        if (intent.getBooleanExtra(EXTRA_RESUME_FULL, false)) {
            String p = intent.getStringExtra(EXTRA_PEER_HEX);
            if (p != null && p.length() == 32) {
                peerHex = p;
            }
            String n = intent.getStringExtra(EXTRA_PEER_DISPLAY_NAME);
            if (n != null) {
                peerDisplayName = n;
            }
            loadPeerUi();
            VoiceCallForegroundService.startOrUpdate(
                    this, peerHex, peerDisplayName, false, elapsedMs());
        }
        String incoming = intent.getStringExtra(EXTRA_INCOMING_JSON);
        if (incoming != null && !incoming.isEmpty() && session != null) {
            session.onRemoteJson(incoming);
        }
    }

    @Override
    protected void onDestroy() {
        VoiceCallCoordinator.clear();
        VoiceCallEngine.setActiveSession(null);
        if (session != null) {
            session.dispose();
            session = null;
        }
        io.shutdown();
        if (audioManager != null) {
            audioManager.setMode(AudioManager.MODE_NORMAL);
            audioManager.setSpeakerphoneOn(false);
        }
        if (activeRef != null && activeRef.get() == this) {
            activeRef = null;
        }
        super.onDestroy();
    }
}
