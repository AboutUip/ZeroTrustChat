package com.kite.zchat;

import android.app.Notification;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.graphics.PixelFormat;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowManager;
import android.widget.ImageView;

import androidx.annotation.Nullable;
import androidx.core.app.NotificationCompat;
import androidx.core.content.ContextCompat;

import androidx.core.app.NotificationManagerCompat;

import com.kite.zchat.push.VoiceCallNotificationHelper;

/**
 * 通话中前台服务：保活麦克风、通知栏返回/挂断、可选悬浮小圆点。
 */
public final class VoiceCallForegroundService extends Service {

    public static final String ACTION_START = "com.kite.zchat.VOICE_FS_START";
    public static final String ACTION_STOP = "com.kite.zchat.VOICE_FS_STOP";
    public static final String ACTION_TOGGLE_SPEAKER_UI = "com.kite.zchat.VOICE_FS_TOGGLE_SPEAKER";

    public static final String EXTRA_PEER_HEX = "peer_hex";
    public static final String EXTRA_PEER_NAME = "peer_name";
    public static final String EXTRA_MINIMIZED = "minimized";
    public static final String EXTRA_ELAPSED_MS = "elapsed_ms";

    private static final int NOTIF_ID = 0x7201;

    private final Handler main = new Handler(Looper.getMainLooper());
    private Runnable tickRunnable;
    private long callStartElapsedMs;
    private String peerHex;
    private String peerName;
    private boolean minimized;
    @Nullable private WindowManager windowManager;
    @Nullable private View bubbleView;
    private int bubbleX;
    private int bubbleY;

    public static void startOrUpdate(
            Context c,
            String peerHex32,
            String peerDisplayName,
            boolean minimized,
            long elapsedSinceConnectMs) {
        Intent i = new Intent(c, VoiceCallForegroundService.class);
        i.setAction(ACTION_START);
        i.putExtra(EXTRA_PEER_HEX, peerHex32);
        i.putExtra(EXTRA_PEER_NAME, peerDisplayName != null ? peerDisplayName : "");
        i.putExtra(EXTRA_MINIMIZED, minimized);
        i.putExtra(EXTRA_ELAPSED_MS, elapsedSinceConnectMs);
        ContextCompat.startForegroundService(c, i);
    }

    public static void stop(Context c) {
        c.stopService(new Intent(c, VoiceCallForegroundService.class));
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent == null) {
            return START_NOT_STICKY;
        }
        String a = intent.getAction();
        if (ACTION_STOP.equals(a)) {
            tearDownBubble();
            stopForeground(STOP_FOREGROUND_REMOVE);
            stopSelf();
            return START_NOT_STICKY;
        }
        if (ACTION_START.equals(a) || intent.getAction() == null) {
            peerHex = intent.getStringExtra(EXTRA_PEER_HEX);
            peerName = intent.getStringExtra(EXTRA_PEER_NAME);
            minimized = intent.getBooleanExtra(EXTRA_MINIMIZED, false);
            callStartElapsedMs = intent.getLongExtra(EXTRA_ELAPSED_MS, 0L);
            if (peerHex == null) {
                peerHex = "";
            }
            startForeground(NOTIF_ID, buildNotification(formatDuration(callStartElapsedMs)));
            scheduleTicks();
            if (minimized && VoiceCallNotificationHelper.canDrawOverlay(this)) {
                showBubbleOverlay();
            } else {
                tearDownBubble();
            }
        }
        return START_STICKY;
    }

    private void scheduleTicks() {
        if (tickRunnable != null) {
            main.removeCallbacks(tickRunnable);
        }
        tickRunnable =
                new Runnable() {
                    @Override
                    public void run() {
                        callStartElapsedMs += 1000;
                        try {
                            NotificationManagerCompat.from(VoiceCallForegroundService.this)
                                    .notify(
                                            NOTIF_ID,
                                            buildNotification(
                                                    formatDuration(callStartElapsedMs)));
                        } catch (SecurityException ignored) {
                        }
                        main.postDelayed(this, 1000);
                    }
                };
        main.postDelayed(tickRunnable, 1000);
    }

    private Notification buildNotification(String durLine) {
        Intent open =
                VoiceCallActivity.buildResumeIntent(
                        this, peerHex, peerName != null ? peerName : "");
        open.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_SINGLE_TOP);
        PendingIntent openPi =
                PendingIntent.getActivity(
                        this,
                        0,
                        open,
                        PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);

        Intent hang =
                new Intent(this, VoiceCallActionReceiver.class)
                        .setAction(VoiceCallActionReceiver.ACTION_HANGUP_ONGOING);
        PendingIntent hangPi =
                PendingIntent.getBroadcast(
                        this,
                        1,
                        hang,
                        PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);

        String title =
                peerName != null && !peerName.isBlank()
                        ? peerName
                        : getString(R.string.voice_call_title);
        return new NotificationCompat.Builder(this, VoiceCallNotificationHelper.CHANNEL_VOICE_CALL)
                .setSmallIcon(R.drawable.ic_notification)
                .setContentTitle(title)
                .setContentText(durLine)
                .setOngoing(true)
                .setCategory(NotificationCompat.CATEGORY_CALL)
                .setContentIntent(openPi)
                .addAction(0, getString(R.string.voice_call_notif_return), openPi)
                .addAction(0, getString(R.string.voice_call_hangup), hangPi)
                .setPriority(NotificationCompat.PRIORITY_LOW)
                .build();
    }

    private static String formatDuration(long elapsedMs) {
        long s = elapsedMs / 1000;
        long m = s / 60;
        s = s % 60;
        long h = m / 60;
        m = m % 60;
        if (h > 0) {
            return String.format(java.util.Locale.US, "%d:%02d:%02d", h, m, s);
        }
        return String.format(java.util.Locale.US, "%d:%02d", m, s);
    }

    private void showBubbleOverlay() {
        tearDownBubble();
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M
                || !android.provider.Settings.canDrawOverlays(this)) {
            return;
        }
        windowManager = (WindowManager) getSystemService(WINDOW_SERVICE);
        if (windowManager == null) {
            return;
        }
        ImageView iv = new ImageView(this);
        iv.setImageResource(R.drawable.ic_notification);
        iv.setBackgroundResource(R.drawable.voice_call_bubble_bg);
        iv.setPadding(24, 24, 24, 24);
        iv.setElevation(8f);
        int size = (int) (56 * getResources().getDisplayMetrics().density);
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) {
            return;
        }
        final WindowManager.LayoutParams p =
                new WindowManager.LayoutParams(
                        size,
                        size,
                        WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY,
                        WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE,
                        PixelFormat.TRANSLUCENT);
        p.gravity = Gravity.TOP | Gravity.START;
        p.x = bubbleX != 0 ? bubbleX : 0;
        p.y = bubbleY != 0 ? bubbleY : 200;
        iv.setOnTouchListener(
                new View.OnTouchListener() {
                    float dx;
                    float dy;
                    float sx;
                    float sy;

                    @Override
                    public boolean onTouch(View v, MotionEvent e) {
                        if (windowManager == null) {
                            return false;
                        }
                        switch (e.getActionMasked()) {
                            case MotionEvent.ACTION_DOWN:
                                dx = p.x - e.getRawX();
                                dy = p.y - e.getRawY();
                                sx = e.getRawX();
                                sy = e.getRawY();
                                return true;
                            case MotionEvent.ACTION_MOVE:
                                p.x = (int) (e.getRawX() + dx);
                                p.y = (int) (e.getRawY() + dy);
                                windowManager.updateViewLayout(v, p);
                                return true;
                            case MotionEvent.ACTION_UP:
                                snapToEdge(v, p);
                                bubbleX = p.x;
                                bubbleY = p.y;
                                if (Math.hypot(e.getRawX() - sx, e.getRawY() - sy) < 20) {
                                    openFullScreen();
                                }
                                return true;
                            default:
                                return false;
                        }
                    }
                });
        try {
            windowManager.addView(iv, p);
            bubbleView = iv;
        } catch (Exception ignored) {
        }
    }

    private void snapToEdge(View v, WindowManager.LayoutParams p) {
        if (windowManager == null) {
            return;
        }
        android.graphics.Point screen = new android.graphics.Point();
        windowManager.getDefaultDisplay().getSize(screen);
        int mid = screen.x / 2;
        int w = v.getWidth();
        if (p.x + w / 2 < mid) {
            p.x = 0;
        } else {
            p.x = screen.x - w;
        }
        try {
            windowManager.updateViewLayout(v, p);
        } catch (Exception ignored) {
        }
    }

    private void openFullScreen() {
        Intent i = VoiceCallActivity.buildResumeIntent(this, peerHex, peerName != null ? peerName : "");
        i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_SINGLE_TOP);
        startActivity(i);
    }

    private void tearDownBubble() {
        if (windowManager != null && bubbleView != null) {
            try {
                windowManager.removeView(bubbleView);
            } catch (Exception ignored) {
            }
        }
        bubbleView = null;
        windowManager = null;
    }

    @Override
    public void onDestroy() {
        if (tickRunnable != null) {
            main.removeCallbacks(tickRunnable);
        }
        tearDownBubble();
        super.onDestroy();
    }

    @Nullable
    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

}
