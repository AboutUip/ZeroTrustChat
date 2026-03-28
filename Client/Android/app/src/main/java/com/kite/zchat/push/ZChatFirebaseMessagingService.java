package com.kite.zchat.push;

import android.content.Context;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;

import com.google.firebase.messaging.FirebaseMessagingService;
import com.google.firebase.messaging.RemoteMessage;

import com.kite.zchat.chat.ChatSync;
import com.kite.zchat.conversation.ConversationPlaceholderStore;
import com.kite.zchat.core.ServerConfigStore;
import com.kite.zchat.core.ServerEndpoint;
import com.kite.zchat.profile.ProfileDisplayHelper;

import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * 数据消息（data payload）离线推送入口：须服务端在业务事件发生时向设备 FCM 令牌发送消息。
 *
 * <p>建议 data 字段（与 {@link #handleData} 一致）：
 *
 * <ul>
 *   <li>{@code type}: {@code chat_message} | {@code friend_request} | {@code friend_removed}
 *   <li>{@code host},{@code port}: 可选，缺省则用 {@link com.kite.zchat.core.ServerConfigStore}
 *   <li>{@code peer_hex}: 32 位 hex（聊天/删好友）
 *   <li>{@code peer_name}: 可选
 *   <li>{@code preview}: 聊天预览（可选，缺省则客户端 SYNC 后取本地预览）
 *   <li>{@code pending_count}: 好友申请数量（可选）
 * </ul>
 */
public final class ZChatFirebaseMessagingService extends FirebaseMessagingService {

    private final ExecutorService io = Executors.newSingleThreadExecutor();

    @Override
    public void onNewToken(@NonNull String token) {
        PushTokenRegistration.onTokenAvailable(getApplicationContext(), token);
    }

    @Override
    public void onMessageReceived(@NonNull RemoteMessage message) {
        Map<String, String> data = message.getData();
        if (data.isEmpty()) {
            return;
        }
        io.execute(() -> handleData(getApplicationContext(), data));
    }

    private static void handleData(Context app, Map<String, String> data) {
        String type = data.get("type");
        if (type == null) {
            return;
        }
        String host = data.get("host");
        int port = parsePort(data.get("port"));
        switch (type) {
            case "chat_message":
                handleChatMessage(app, host, port, data);
                break;
            case "friend_request":
                handleFriendRequest(app, host, port, data);
                break;
            case "friend_removed":
                handleFriendRemoved(app, host, port, data);
                break;
            default:
                break;
        }
    }

    private static int parsePort(@androidx.annotation.Nullable String s) {
        if (s == null || s.isBlank()) {
            return 0;
        }
        try {
            return Integer.parseInt(s.trim());
        } catch (NumberFormatException e) {
            return 0;
        }
    }

    private static void handleChatMessage(Context app, String host, int port, Map<String, String> data) {
        String peerHex = data.get("peer_hex");
        if (peerHex == null || peerHex.length() != 32) {
            return;
        }
        if (ConversationPlaceholderStore.isPeerMuted(app, peerHex)) {
            return;
        }
        ServerEndpoint ep = resolveHostPort(app, host, port);
        if (ep == null) {
            return;
        }
        ChatSync.syncPeer(app.getApplicationContext(), ep.host(), ep.port(), peerHex, true);
        String nameStored = ConversationPlaceholderStore.getPeerDisplayNameStored(app, peerHex);
        String title = ProfileDisplayHelper.chatBubblePeerName(app, nameStored != null ? nameStored : "");
        String preview = data.get("preview");
        if (preview == null || preview.isBlank()) {
            preview = ConversationPlaceholderStore.getLastMessagePreview(app, peerHex);
        }
        if (preview == null || preview.isBlank()) {
            preview = app.getString(com.kite.zchat.R.string.notification_message_default_preview);
        }
        ZChatNotificationHelper.showChatMessage(app, ep.host(), ep.port(), peerHex, title, preview);
    }

    private static void handleFriendRequest(Context app, String host, int port, Map<String, String> data) {
        ServerEndpoint ep = resolveHostPort(app, host, port);
        if (ep == null) {
            return;
        }
        int pending = parseIntSafe(data.get("pending_count"), 1);
        if (pending < 1) {
            pending = 1;
        }
        ZChatNotificationHelper.showFriendRequestNotification(app, ep.host(), ep.port(), pending);
        FriendRequestState.setLastKnownPendingCount(app, pending);
    }

    private static void handleFriendRemoved(Context app, String host, int port, Map<String, String> data) {
        ServerEndpoint ep = resolveHostPort(app, host, port);
        if (ep == null) {
            return;
        }
        String peerHex = data.get("peer_hex");
        if (peerHex == null || peerHex.length() != 32) {
            peerHex = "";
        }
        String peerName = data.get("peer_name");
        ZChatNotificationHelper.showFriendRemovedNotification(
                app, ep.host(), ep.port(), peerHex, peerName);
    }

    @Nullable
    private static ServerEndpoint resolveHostPort(Context app, String host, int port) {
        if (host != null && !host.isBlank() && port > 0 && port <= 65535) {
            return new ServerEndpoint(host.trim(), port);
        }
        return new ServerConfigStore(app).getSavedEndpoint();
    }

    private static int parseIntSafe(@androidx.annotation.Nullable String s, int defaultValue) {
        if (s == null || s.isBlank()) {
            return defaultValue;
        }
        try {
            return Integer.parseInt(s.trim());
        } catch (NumberFormatException e) {
            return defaultValue;
        }
    }

    @Override
    public void onDestroy() {
        io.shutdownNow();
        super.onDestroy();
    }
}
