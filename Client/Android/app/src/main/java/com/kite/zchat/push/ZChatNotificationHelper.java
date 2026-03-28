package com.kite.zchat.push;

import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.os.Build;

import androidx.annotation.Nullable;
import androidx.core.app.NotificationCompat;
import androidx.core.app.NotificationManagerCompat;

import com.kite.zchat.ChatActivity;
import com.kite.zchat.MainPlaceholderActivity;
import com.kite.zchat.R;
import com.kite.zchat.core.ServerConfigStore;
import com.kite.zchat.core.ServerEndpoint;

/** 系统通知渠道与展示；点击通知进入 {@link MainPlaceholderActivity} 或子页面。 */
public final class ZChatNotificationHelper {

    public static final String CHANNEL_MESSAGES = "zchat_messages";
    public static final String CHANNEL_FRIEND_REQUESTS = "zchat_friend_requests";
    public static final String CHANNEL_FRIEND_EVENTS = "zchat_friend_events";

    private static final int ID_FRIEND_REQUEST = 0x2000;
    private static final int ID_FRIEND_REMOVED_BASE = 0x3000;

    private ZChatNotificationHelper() {}

    public static void createChannels(Context context) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) {
            return;
        }
        NotificationManager nm = context.getSystemService(NotificationManager.class);
        if (nm == null) {
            return;
        }
        nm.createNotificationChannel(
                new NotificationChannel(
                        CHANNEL_MESSAGES,
                        context.getString(R.string.notification_channel_messages),
                        NotificationManager.IMPORTANCE_DEFAULT));
        nm.createNotificationChannel(
                new NotificationChannel(
                        CHANNEL_FRIEND_REQUESTS,
                        context.getString(R.string.notification_channel_friend_requests),
                        NotificationManager.IMPORTANCE_DEFAULT));
        nm.createNotificationChannel(
                new NotificationChannel(
                        CHANNEL_FRIEND_EVENTS,
                        context.getString(R.string.notification_channel_friend_events),
                        NotificationManager.IMPORTANCE_DEFAULT));
    }

    public static void showChatMessage(
            Context app,
            String host,
            int port,
            String peerHex32,
            String peerDisplayName,
            String previewText) {
        if (!canNotify(app)) {
            return;
        }
        String title =
                peerDisplayName != null && !peerDisplayName.isBlank()
                        ? peerDisplayName
                        : app.getString(R.string.notification_message_title_fallback);
        String body =
                previewText != null && !previewText.isBlank()
                        ? previewText
                        : app.getString(R.string.notification_message_default_preview);
        Intent i =
                buildMainIntent(app, host, port)
                        .putExtra(NotificationNavExtras.EXTRA_NAV_TARGET, NotificationNavExtras.NAV_OPEN_CHAT)
                        .putExtra(ChatActivity.EXTRA_PEER_USER_ID_HEX, peerHex32)
                        .putExtra(ChatActivity.EXTRA_PEER_DISPLAY_NAME, peerDisplayName != null ? peerDisplayName : "");
        PendingIntent pi = pendingMain(app, i, peerHex32.hashCode() & 0xffff);
        NotificationCompat.Builder b =
                new NotificationCompat.Builder(app, CHANNEL_MESSAGES)
                        .setSmallIcon(R.drawable.ic_notification)
                        .setContentTitle(title)
                        .setContentText(body)
                        .setStyle(new NotificationCompat.BigTextStyle().bigText(body))
                        .setAutoCancel(true)
                        .setContentIntent(pi)
                        .setPriority(NotificationCompat.PRIORITY_DEFAULT)
                        .setCategory(NotificationCompat.CATEGORY_MESSAGE);
        notifySafe(app, peerHex32.hashCode(), b.build());
    }

    public static void showFriendRequestNotification(
            Context app, String host, int port, int pendingCount) {
        if (!canNotify(app)) {
            return;
        }
        String title = app.getString(R.string.notification_friend_request_title);
        String body =
                app.getString(
                        R.string.notification_friend_request_body,
                        Math.min(pendingCount, 999));
        Intent i =
                buildMainIntent(app, host, port)
                        .putExtra(
                                NotificationNavExtras.EXTRA_NAV_TARGET,
                                NotificationNavExtras.NAV_FRIEND_REQUESTS);
        PendingIntent pi = pendingMain(app, i, ID_FRIEND_REQUEST);
        NotificationCompat.Builder b =
                new NotificationCompat.Builder(app, CHANNEL_FRIEND_REQUESTS)
                        .setSmallIcon(R.drawable.ic_notification)
                        .setContentTitle(title)
                        .setContentText(body)
                        .setAutoCancel(true)
                        .setContentIntent(pi)
                        .setPriority(NotificationCompat.PRIORITY_DEFAULT)
                        .setCategory(NotificationCompat.CATEGORY_SOCIAL);
        notifySafe(app, ID_FRIEND_REQUEST, b.build());
    }

    public static void showFriendRemovedNotification(
            Context app,
            String host,
            int port,
            String peerHex32,
            @Nullable String peerName) {
        if (!canNotify(app)) {
            return;
        }
        String title = app.getString(R.string.notification_friend_removed_title);
        String who =
                peerName != null && !peerName.isBlank()
                        ? peerName
                        : (peerHex32 != null ? peerHex32 : "?");
        String body = app.getString(R.string.notification_friend_removed_body, who);
        int req = ID_FRIEND_REMOVED_BASE + (peerHex32 != null ? peerHex32.hashCode() & 0xfff : 0);
        Intent i =
                buildMainIntent(app, host, port)
                        .putExtra(
                                NotificationNavExtras.EXTRA_NAV_TARGET,
                                NotificationNavExtras.NAV_FRIEND_REMOVED);
        PendingIntent pi = pendingMain(app, i, req);
        NotificationCompat.Builder b =
                new NotificationCompat.Builder(app, CHANNEL_FRIEND_EVENTS)
                        .setSmallIcon(R.drawable.ic_notification)
                        .setContentTitle(title)
                        .setContentText(body)
                        .setStyle(new NotificationCompat.BigTextStyle().bigText(body))
                        .setAutoCancel(true)
                        .setContentIntent(pi)
                        .setPriority(NotificationCompat.PRIORITY_DEFAULT)
                        .setCategory(NotificationCompat.CATEGORY_STATUS);
        notifySafe(app, req, b.build());
    }

    private static boolean canNotify(Context context) {
        if (Build.VERSION.SDK_INT < 33) {
            return true;
        }
        return NotificationManagerCompat.from(context).areNotificationsEnabled();
    }

    private static Intent buildMainIntent(Context app, String host, int port) {
        String h = host;
        int p = port;
        if (h == null || h.isBlank() || p <= 0) {
            ServerEndpoint ep = new ServerConfigStore(app).getSavedEndpoint();
            if (ep != null) {
                h = ep.host();
                p = ep.port();
            }
        }
        Intent i = new Intent(app, MainPlaceholderActivity.class);
        i.putExtra(MainPlaceholderActivity.EXTRA_HOST, h);
        i.putExtra(MainPlaceholderActivity.EXTRA_PORT, p);
        i.putExtra(NotificationNavExtras.EXTRA_FROM_NOTIFICATION, true);
        i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP | Intent.FLAG_ACTIVITY_SINGLE_TOP);
        return i;
    }

    private static PendingIntent pendingMain(Context app, Intent mainIntent, int requestCode) {
        return PendingIntent.getActivity(
                app,
                requestCode,
                mainIntent,
                PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
    }

    private static void notifySafe(Context app, int id, android.app.Notification n) {
        try {
            NotificationManagerCompat.from(app).notify(id, n);
        } catch (SecurityException ignored) {
        }
    }
}
