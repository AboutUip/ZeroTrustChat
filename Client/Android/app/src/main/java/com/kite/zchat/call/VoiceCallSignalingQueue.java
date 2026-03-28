package com.kite.zchat.call;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

/**
 * 接听前 WebRTC 会话尚未创建时，先缓存该 peer 的 answer/ice 等信令。
 */
public final class VoiceCallSignalingQueue {

    private static final Map<String, List<String>> PENDING = new ConcurrentHashMap<>();

    private VoiceCallSignalingQueue() {}

    public static void enqueue(String peerHex32, String jsonPayload) {
        if (peerHex32 == null || peerHex32.length() != 32 || jsonPayload == null) {
            return;
        }
        String k = peerHex32.toLowerCase(Locale.ROOT);
        synchronized (PENDING) {
            PENDING.computeIfAbsent(k, x -> new ArrayList<>()).add(jsonPayload);
        }
    }

    public static void drainToSession(String peerHex32, WebRtcAudioCallSession session) {
        if (peerHex32 == null || session == null) {
            return;
        }
        String k = peerHex32.toLowerCase(Locale.ROOT);
        List<String> list;
        synchronized (PENDING) {
            list = PENDING.remove(k);
        }
        if (list == null) {
            return;
        }
        for (String j : list) {
            session.onRemoteJson(j);
        }
    }

    public static void clearPeer(String peerHex32) {
        if (peerHex32 == null) {
            return;
        }
        PENDING.remove(peerHex32.toLowerCase(Locale.ROOT));
    }
}
