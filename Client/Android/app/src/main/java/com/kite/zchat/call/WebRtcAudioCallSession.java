package com.kite.zchat.call;

import android.content.Context;

import androidx.annotation.Nullable;

import com.kite.zchat.R;
import com.kite.zchat.auth.AuthCredentialStore;

import org.json.JSONException;
import org.json.JSONObject;
import org.webrtc.AudioSource;
import org.webrtc.AudioTrack;
import org.webrtc.IceCandidate;
import org.webrtc.MediaConstraints;
import org.webrtc.MediaStreamTrack;
import org.webrtc.PeerConnection;
import org.webrtc.PeerConnectionFactory;
import org.webrtc.RtpReceiver;
import org.webrtc.SdpObserver;
import org.webrtc.SessionDescription;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * 纯音频 WebRTC 会话：SDP/ICE 经 {@link WebRtcSignaling#PREFIX} 文本发送。
 */
public final class WebRtcAudioCallSession {

    public interface Listener {
        void onStatus(String line);

        void onError(String message);

        void onEnded();

        /**
         * 双方同时发起呼叫（均有本地 offer）：按 hex 较小一侧放弃主叫、接听对方 offer。
         */
        default void onGlareAsCallee(String remoteOfferSdp) {}
    }

    public interface SignalingSender {
        /** @return false 若未连上服务器或发送失败 */
        boolean sendSignalingJson(String json);
    }

    private final Context appContext;
    private final String peerHex;
    private final SignalingSender sender;
    private final Listener listener;
    private final ExecutorService exec = Executors.newSingleThreadExecutor();

    private PeerConnectionFactory factory;
    @Nullable private PeerConnection pc;
    @Nullable private AudioSource audioSource;
    @Nullable private AudioTrack localAudioTrack;

    private boolean muted;
    private boolean disposed;
    private boolean remoteDescriptionSet;
    private boolean endedNotified;
    private boolean suppressEndCallback;
    private final ArrayList<IceCandidate> pendingRemoteIce = new ArrayList<>();

    public WebRtcAudioCallSession(
            Context context,
            String peerHex32,
            SignalingSender sender,
            Listener listener) {
        this.appContext = context.getApplicationContext();
        this.peerHex = peerHex32;
        this.sender = sender;
        this.listener = listener;
    }

    public String getPeerHex() {
        return peerHex;
    }

    public void startCaller() {
        exec.execute(
                () -> {
                    if (disposed) {
                        return;
                    }
                    ensurePcAndLocalTrack();
                    if (pc == null) {
                        return;
                    }
                    MediaConstraints offerConstraints = new MediaConstraints();
                    pc.createOffer(
                            new SdpObserverAdapter() {
                                @Override
                                public void onCreateSuccess(SessionDescription sessionDescription) {
                                    exec.execute(
                                            () -> {
                                                if (disposed || pc == null) {
                                                    return;
                                                }
                                                pc.setLocalDescription(
                                                        new SdpObserverAdapter() {
                                                            @Override
                                                            public void onSetSuccess() {
                                                                sendSdp("offer", sessionDescription.description);
                                                            }

                                                            @Override
                                                            public void onSetFailure(String s) {
                                                                fail(s);
                                                            }
                                                        },
                                                        sessionDescription);
                                            });
                                }

                                @Override
                                public void onCreateFailure(String s) {
                                    fail(s != null ? s : "createOffer");
                                }
                            },
                            offerConstraints);
                });
    }

    /** 被叫：首帧为远端 offer。 */
    public void startCalleeWithOffer(String sdpOffer) {
        exec.execute(
                () -> {
                    if (disposed) {
                        return;
                    }
                    ensurePcAndLocalTrack();
                    if (pc == null) {
                        return;
                    }
                    SessionDescription offer =
                            new SessionDescription(SessionDescription.Type.OFFER, sdpOffer);
                    pc.setRemoteDescription(
                            new SdpObserverAdapter() {
                                @Override
                                public void onSetSuccess() {
                                    remoteDescriptionSet = true;
                                    drainPendingIce();
                                    createAnswerAfterRemoteOffer();
                                }

                                @Override
                                public void onSetFailure(String s) {
                                    fail(s);
                                }
                            },
                            offer);
                });
    }

    private void createAnswerAfterRemoteOffer() {
        if (pc == null || disposed) {
            return;
        }
        pc.createAnswer(
                new SdpObserverAdapter() {
                    @Override
                    public void onCreateSuccess(SessionDescription sessionDescription) {
                        exec.execute(
                                () -> {
                                    if (disposed || pc == null) {
                                        return;
                                    }
                                    pc.setLocalDescription(
                                            new SdpObserverAdapter() {
                                                @Override
                                                public void onSetSuccess() {
                                                    sendSdp("answer", sessionDescription.description);
                                                }

                                                @Override
                                                public void onSetFailure(String s) {
                                                    fail(s);
                                                }
                                            },
                                            sessionDescription);
                                });
                    }

                    @Override
                    public void onCreateFailure(String s) {
                        fail(s != null ? s : "createAnswer");
                    }
                },
                new MediaConstraints());
    }

    public void onRemoteJson(String json) {
        exec.execute(() -> handleRemoteJson(json));
    }

    private void handleRemoteJson(String json) {
        if (disposed || json == null || json.isEmpty()) {
            return;
        }
        try {
            JSONObject o = new JSONObject(json);
            String t = o.optString("t");
            switch (t) {
                case "offer":
                    if (remoteDescriptionSet) {
                        return;
                    }
                    String offerSdp = o.optString("sdp");
                    if (offerSdp.isEmpty()) {
                        return;
                    }
                    if (pc != null
                            && pc.signalingState()
                                    == PeerConnection.SignalingState.HAVE_LOCAL_OFFER) {
                        String selfHex = AuthCredentialStore.create(appContext).getUserIdHex();
                        if (selfHex.length() == 32) {
                            if (selfHex.compareToIgnoreCase(peerHex) < 0) {
                                listener.onGlareAsCallee(offerSdp);
                            }
                        }
                        return;
                    }
                    if (pc != null
                            && pc.signalingState() != PeerConnection.SignalingState.STABLE) {
                        return;
                    }
                    startCalleeWithOffer(offerSdp);
                    break;
                case "answer":
                    if (pc == null) {
                        return;
                    }
                    String answerSdp = o.optString("sdp");
                    if (answerSdp.isEmpty()) {
                        return;
                    }
                    SessionDescription ans =
                            new SessionDescription(SessionDescription.Type.ANSWER, answerSdp);
                    pc.setRemoteDescription(
                            new SdpObserverAdapter() {
                                @Override
                                public void onSetSuccess() {
                                    remoteDescriptionSet = true;
                                    drainPendingIce();
                                    status(
                                            appContext.getString(
                                                    R.string.voice_call_status_connecting));
                                }

                                @Override
                                public void onSetFailure(String s) {
                                    fail(s);
                                }
                            },
                            ans);
                    break;
                case "ice":
                    addIceFromJson(o);
                    break;
                case "bye":
                    endLocal();
                    break;
                case "busy":
                    exec.execute(
                            () -> {
                                if (disposed) {
                                    return;
                                }
                                releaseMedia();
                                listener.onError("BUSY");
                            });
                    break;
                case "reject":
                    exec.execute(
                            () -> {
                                if (disposed) {
                                    return;
                                }
                                releaseMedia();
                                listener.onError("REJECT");
                            });
                    break;
                default:
                    break;
            }
        } catch (JSONException e) {
            fail(e.getMessage() != null ? e.getMessage() : "json");
        }
    }

    private void addIceFromJson(JSONObject o) {
        String cand = o.optString("candidate");
        if (cand.isEmpty()) {
            return;
        }
        String mid = o.has("sdpMid") && !o.isNull("sdpMid") ? o.optString("sdpMid") : null;
        int line = o.optInt("sdpMLineIndex", 0);
        IceCandidate ice = new IceCandidate(mid, line, cand);
        if (pc == null || !remoteDescriptionSet) {
            pendingRemoteIce.add(ice);
            return;
        }
        pc.addIceCandidate(ice);
    }

    private void drainPendingIce() {
        if (pc == null || pendingRemoteIce.isEmpty()) {
            return;
        }
        for (IceCandidate c : pendingRemoteIce) {
            pc.addIceCandidate(c);
        }
        pendingRemoteIce.clear();
    }

    private void sendSdp(String type, String sdp) {
        try {
            JSONObject o = new JSONObject();
            o.put("v", 1);
            o.put("t", type);
            o.put("sdp", sdp);
            if (!sender.sendSignalingJson(o.toString())) {
                fail(appContext.getString(R.string.voice_call_signaling_failed));
            }
        } catch (JSONException e) {
            fail(e.getMessage());
        }
    }

    /**
     * 仅 STUN 在移动网/对称 NAT 下常无法打洞；补充公共 TURN（Open Relay）与多 STUN，并启用 TCP candidate。
     */
    private static List<PeerConnection.IceServer> buildIceServers() {
        List<PeerConnection.IceServer> list = new ArrayList<>();
        list.add(PeerConnection.IceServer.builder("stun:stun.l.google.com:19302").createIceServer());
        list.add(PeerConnection.IceServer.builder("stun:stun1.l.google.com:19302").createIceServer());
        PeerConnection.IceServer.Builder turnUdp =
                PeerConnection.IceServer.builder("turn:openrelay.metered.ca:80");
        turnUdp.setUsername("openrelayproject");
        turnUdp.setPassword("openrelayproject");
        list.add(turnUdp.createIceServer());
        PeerConnection.IceServer.Builder turnTcp =
                PeerConnection.IceServer.builder("turn:openrelay.metered.ca:443?transport=tcp");
        turnTcp.setUsername("openrelayproject");
        turnTcp.setPassword("openrelayproject");
        list.add(turnTcp.createIceServer());
        return list;
    }

    private void sendIce(IceCandidate ice) {
        try {
            JSONObject o = new JSONObject();
            o.put("v", 1);
            o.put("t", "ice");
            o.put("candidate", ice.sdp);
            if (ice.sdpMid != null) {
                o.put("sdpMid", ice.sdpMid);
            }
            o.put("sdpMLineIndex", ice.sdpMLineIndex);
            if (!sender.sendSignalingJson(o.toString())) {
                fail(appContext.getString(R.string.voice_call_signaling_failed));
            }
        } catch (JSONException e) {
            fail(e.getMessage());
        }
    }

    private void ensurePcAndLocalTrack() {
        if (pc != null) {
            return;
        }
        factory = WebRtcPeerConnectionFactoryHolder.get(appContext);
        PeerConnection.RTCConfiguration cfg = new PeerConnection.RTCConfiguration(buildIceServers());
        cfg.sdpSemantics = PeerConnection.SdpSemantics.UNIFIED_PLAN;
        cfg.tcpCandidatePolicy = PeerConnection.TcpCandidatePolicy.ENABLED;
        cfg.bundlePolicy = PeerConnection.BundlePolicy.MAXBUNDLE;
        cfg.rtcpMuxPolicy = PeerConnection.RtcpMuxPolicy.REQUIRE;
        cfg.continualGatheringPolicy = PeerConnection.ContinualGatheringPolicy.GATHER_CONTINUALLY;

        pc =
                factory.createPeerConnection(
                        cfg,
                        new PeerConnection.Observer() {
                            @Override
                            public void onSignalingChange(
                                    PeerConnection.SignalingState signalingState) {}

                            @Override
                            public void onIceConnectionChange(
                                    PeerConnection.IceConnectionState state) {
                                if (state == PeerConnection.IceConnectionState.FAILED) {
                                    fail(
                                            appContext.getString(
                                                    R.string.voice_call_ice_failed));
                                } else if (state == PeerConnection.IceConnectionState.CONNECTED
                                        || state == PeerConnection.IceConnectionState.COMPLETED) {
                                    status(
                                            appContext.getString(
                                                    R.string.voice_call_status_in_call));
                                }
                            }

                            @Override
                            public void onIceConnectionReceivingChange(boolean receiving) {}

                            @Override
                            public void onIceGatheringChange(
                                    PeerConnection.IceGatheringState iceGatheringState) {}

                            @Override
                            public void onIceCandidate(IceCandidate iceCandidate) {
                                exec.execute(() -> sendIce(iceCandidate));
                            }

                            @Override
                            public void onIceCandidatesRemoved(IceCandidate[] iceCandidates) {}

                            @Override
                            public void onAddStream(org.webrtc.MediaStream mediaStream) {}

                            @Override
                            public void onRemoveStream(org.webrtc.MediaStream mediaStream) {}

                            @Override
                            public void onDataChannel(org.webrtc.DataChannel dataChannel) {}

                            @Override
                            public void onRenegotiationNeeded() {}

                            @Override
                            public void onAddTrack(
                                    RtpReceiver rtpReceiver,
                                    org.webrtc.MediaStream[] mediaStreams) {
                                MediaStreamTrack t = rtpReceiver.track();
                                if (t instanceof AudioTrack) {
                                    ((AudioTrack) t).setEnabled(true);
                                }
                            }
                        });
        if (pc == null) {
            fail("无法创建 PeerConnection");
            return;
        }
        MediaConstraints audioConstraints = new MediaConstraints();
        audioSource = factory.createAudioSource(audioConstraints);
        localAudioTrack = factory.createAudioTrack("local_audio", audioSource);
        localAudioTrack.setEnabled(!muted);
        pc.addTrack(localAudioTrack, Collections.emptyList());
        status(appContext.getString(R.string.voice_call_status_negotiating));
    }

    public void setMuted(boolean mute) {
        muted = mute;
        exec.execute(
                () -> {
                    if (localAudioTrack != null) {
                        localAudioTrack.setEnabled(!mute);
                    }
                });
    }

    public void hangup() {
        exec.execute(
                () -> {
                    try {
                        JSONObject o = new JSONObject();
                        o.put("v", 1);
                        o.put("t", "bye");
                        sender.sendSignalingJson(o.toString());
                    } catch (JSONException ignored) {
                    }
                    endLocal();
                });
    }

    private void endLocal() {
        if (disposed) {
            return;
        }
        releaseMedia();
        if (!endedNotified) {
            endedNotified = true;
            if (!suppressEndCallback) {
                listener.onEnded();
            }
        }
    }

    /**  glare 切换会话时释放资源，不触发 {@link Listener#onEnded()}。 */
    public void disposeSilently() {
        suppressEndCallback = true;
        dispose();
    }

    private void releaseMedia() {
        if (disposed) {
            return;
        }
        disposed = true;
        if (pc != null) {
            pc.dispose();
            pc = null;
        }
        if (localAudioTrack != null) {
            localAudioTrack.dispose();
            localAudioTrack = null;
        }
        if (audioSource != null) {
            audioSource.dispose();
            audioSource = null;
        }
        pendingRemoteIce.clear();
    }

    /** Activity 销毁时调用：若尚未挂断则释放资源。 */
    public void dispose() {
        try {
            exec.execute(
                    () -> {
                        if (!disposed) {
                            endLocal();
                        }
                    });
        } catch (java.util.concurrent.RejectedExecutionException ignored) {
        }
        exec.shutdown();
    }

    private void status(String s) {
        listener.onStatus(s);
    }

    private void fail(String s) {
        listener.onError(s != null ? s : "error");
    }

    private static class SdpObserverAdapter implements SdpObserver {
        @Override
        public void onCreateSuccess(SessionDescription sessionDescription) {}

        @Override
        public void onSetSuccess() {}

        @Override
        public void onCreateFailure(String s) {}

        @Override
        public void onSetFailure(String s) {}
    }
}
