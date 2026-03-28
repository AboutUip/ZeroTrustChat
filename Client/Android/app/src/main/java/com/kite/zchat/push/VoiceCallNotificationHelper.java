package com.kite.zchat.push;

import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.os.Build;
import android.provider.Settings;

import androidx.core.app.NotificationCompat;
import androidx.core.app.NotificationManagerCompat;

import com.kite.zchat.R;
import com.kite.zchat.VoiceCallActionReceiver;
import com.kite.zchat.VoiceCallActivity;

/** 语音来电全屏/高优先级通知与来电通知取消。 */
public final class VoiceCallNotificationHelper {

    public static final String CHANNEL_VOICE_CALL = "zchat_voice_call";

    private static final int ID_INCOMING_BASE = 0x7100;

    private VoiceCallNotificationHelper() {}

    public static void createChannel(Context context) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) {
            return;
        }
        NotificationManager nm = context.getSystemService(NotificationManager.class);
        if (nm == null) {
            return;
        }
        NotificationChannel ch =
                new NotificationChannel(
                        CHANNEL_VOICE_CALL,
                        context.getString(R.string.notification_channel_voice_call),
                        NotificationManager.IMPORTANCE_HIGH);
        ch.setDescription(context.getString(R.string.notification_channel_voice_call_desc));
        ch.enableVibration(true);
        ch.setLockscreenVisibility(android.app.Notification.VISIBILITY_PUBLIC);
        nm.createNotificationChannel(ch);
    }

    public static int incomingNotificationId(String peerHex32) {
        return ID_INCOMING_BASE + (peerHex32 != null ? peerHex32.hashCode() & 0xfff : 0);
    }

    public static void showIncomingCall(
            Context app,
            String peerHex32,
            String peerDisplayName,
            String offerJson,
            @androidx.annotation.Nullable String host,
            int port) {
        if (!canNotify(app)) {
            return;
        }
        String title =
                peerDisplayName != null && !peerDisplayName.isBlank()
                        ? peerDisplayName
                        : app.getString(R.string.voice_call_title);
        Intent full =
                VoiceCallActivity.buildIncomingIntent(
                        app, host, port, peerHex32, offerJson, false);
        full.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP);
        int req = incomingNotificationId(peerHex32);
        PendingIntent fullPi =
                PendingIntent.getActivity(
                        app,
                        req,
                        full,
                        PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);

        Intent decline =
                new Intent(app, VoiceCallActionReceiver.class)
                        .setAction(VoiceCallActionReceiver.ACTION_DECLINE_INCOMING)
                        .putExtra(VoiceCallActionReceiver.EXTRA_PEER_HEX, peerHex32);
        PendingIntent declinePi =
                PendingIntent.getBroadcast(
                        app,
                        req + 1,
                        decline,
                        PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);

        Intent answer =
                VoiceCallActivity.buildIncomingIntent(
                        app, host, port, peerHex32, offerJson, true);
        answer.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP);
        PendingIntent answerPi =
                PendingIntent.getActivity(
                        app,
                        req + 2,
                        answer,
                        PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);

        NotificationCompat.Builder b =
                new NotificationCompat.Builder(app, CHANNEL_VOICE_CALL)
                        .setSmallIcon(R.drawable.ic_notification)
                        .setContentTitle(title)
                        .setContentText(app.getString(R.string.voice_call_notif_incoming_body))
                        .setCategory(NotificationCompat.CATEGORY_CALL)
                        .setPriority(NotificationCompat.PRIORITY_MAX)
                        .setAutoCancel(true)
                        .setContentIntent(fullPi)
                        .setFullScreenIntent(fullPi, true)
                        .addAction(
                                0,
                                app.getString(R.string.voice_call_notif_decline),
                                declinePi)
                        .addAction(
                                0,
                                app.getString(R.string.voice_call_notif_answer),
                                answerPi)
                        .setOngoing(false)
                        .setVisibility(NotificationCompat.VISIBILITY_PUBLIC);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            b.setForegroundServiceBehavior(NotificationCompat.FOREGROUND_SERVICE_IMMEDIATE);
        }
        try {
            NotificationManagerCompat.from(app).notify(req, b.build());
        } catch (SecurityException ignored) {
        }
    }

    public static void cancelIncoming(Context app, String peerHex32) {
        try {
            NotificationManagerCompat.from(app).cancel(incomingNotificationId(peerHex32));
        } catch (SecurityException ignored) {
        }
    }

    private static boolean canNotify(Context context) {
        if (Build.VERSION.SDK_INT < 33) {
            return true;
        }
        return NotificationManagerCompat.from(context).areNotificationsEnabled();
    }

    public static boolean canDrawOverlay(Context c) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M) {
            return true;
        }
        return Settings.canDrawOverlays(c);
    }
}
