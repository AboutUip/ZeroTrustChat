package com.kite.zchat;

import android.Manifest;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.res.Configuration;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.View;

import androidx.annotation.Nullable;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;
import androidx.core.graphics.Insets;
import androidx.core.view.ViewCompat;
import androidx.core.view.WindowCompat;
import androidx.core.view.WindowInsetsCompat;
import androidx.core.view.WindowInsetsControllerCompat;
import androidx.viewpager2.widget.ViewPager2;

import com.google.android.material.badge.BadgeDrawable;
import com.google.android.material.bottomnavigation.BottomNavigationView;

import com.kite.zchat.auth.AuthCredentialStore;
import com.kite.zchat.chat.ChatSync;
import com.kite.zchat.core.ServerConfigStore;
import com.kite.zchat.core.ServerEndpoint;
import com.kite.zchat.friends.FriendPendingRequests;
import com.kite.zchat.main.MainTabPagerAdapter;
import com.kite.zchat.push.FriendRequestState;
import com.kite.zchat.push.NotificationNavExtras;
import com.kite.zchat.push.PendingNavigation;
import com.kite.zchat.push.PushTokenRegistration;
import com.kite.zchat.ui.animation.TabPageTransforms;

import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public class MainPlaceholderActivity extends AppCompatActivity {

    public static final String EXTRA_HOST = LoginActivity.EXTRA_HOST;
    public static final String EXTRA_PORT = LoginActivity.EXTRA_PORT;
    /** 与 {@link Intent#FLAG_ACTIVITY_CLEAR_TOP} 配合：回到主界面并选中通信 Tab。 */
    public static final String EXTRA_OPEN_COMMUNICATION_TAB = "open_communication_tab";

    private static final int[] BOTTOM_NAV_IDS = {
            R.id.nav_communication,
            R.id.nav_contacts,
            R.id.nav_features,
            R.id.nav_profile
    };

    private static final int REQ_POST_NOTIFICATIONS = 0x5821;

    /**
     * 单连接 ZSP 下新消息依赖周期 SYNC 读 socket；间隔过长会导致列表/通知明显滞后（原 20s 约等于用户感知的十几秒延迟）。
     */
    private static final long OPEN_SESSIONS_SYNC_MS = 4_000L;

    private final ExecutorService badgeExecutor = Executors.newSingleThreadExecutor();
    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private final Runnable openSessionsSyncRunnable =
            new Runnable() {
                @Override
                public void run() {
                    if (isFinishing()) {
                        return;
                    }
                    if (serverHost == null || serverHost.isBlank() || serverPort <= 0) {
                        mainHandler.postDelayed(this, OPEN_SESSIONS_SYNC_MS);
                        return;
                    }
                    badgeExecutor.execute(
                            () ->
                                    ChatSync.syncAllOpenSessions(
                                            getApplicationContext(), serverHost, serverPort));
                    mainHandler.postDelayed(this, OPEN_SESSIONS_SYNC_MS);
                }
            };

    private String serverHost;
    private int serverPort;
    private BottomNavigationView bottomNav;
    private ViewPager2 viewPager;

    public static void start(Context context, ServerEndpoint endpoint) {
        Intent i = new Intent(context, MainPlaceholderActivity.class);
        i.putExtra(EXTRA_HOST, endpoint.host());
        i.putExtra(EXTRA_PORT, endpoint.port());
        if (!(context instanceof android.app.Activity)) {
            i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        }
        context.startActivity(i);
    }

    @Override
    protected void onCreate(@Nullable Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        if (!AuthCredentialStore.create(this).hasCredentials()) {
            ServerEndpoint ep = resolveServerEndpoint();
            if (ep == null) {
                startActivity(new Intent(this, MainActivity.class));
                finish();
                return;
            }
            if (getIntent().getBooleanExtra(NotificationNavExtras.EXTRA_FROM_NOTIFICATION, false)) {
                PendingNavigation.saveFromIntent(getIntent());
            }
            LoginActivity.start(this, ep, getString(R.string.notification_login_required));
            finish();
            return;
        }
        WindowCompat.setDecorFitsSystemWindows(getWindow(), false);
        setContentView(R.layout.activity_main_placeholder);

        boolean night =
                (getResources().getConfiguration().uiMode & Configuration.UI_MODE_NIGHT_MASK)
                        == Configuration.UI_MODE_NIGHT_YES;
        WindowInsetsControllerCompat insetsController =
                WindowCompat.getInsetsController(getWindow(), getWindow().getDecorView());
        insetsController.setAppearanceLightStatusBars(!night);
        insetsController.setAppearanceLightNavigationBars(!night);

        View mainRoot = findViewById(R.id.mainRoot);
        ViewCompat.setOnApplyWindowInsetsListener(
                mainRoot,
                (v, windowInsets) -> {
                    Insets bars = windowInsets.getInsets(WindowInsetsCompat.Type.systemBars());
                    Insets cutout = windowInsets.getInsets(WindowInsetsCompat.Type.displayCutout());
                    // 底边不预留：底部导航栏背景延伸至屏幕最底，系统「小白条」区域与导航栏同色嵌入
                    v.setPadding(
                            Math.max(bars.left, cutout.left),
                            Math.max(bars.top, cutout.top),
                            Math.max(bars.right, cutout.right),
                            0);
                    return windowInsets;
                });

        serverHost = getIntent().getStringExtra(EXTRA_HOST);
        serverPort = getIntent().getIntExtra(EXTRA_PORT, 0);
        if (serverHost == null || serverHost.isBlank() || serverPort <= 0) {
            ServerEndpoint ep = new ServerConfigStore(this).getSavedEndpoint();
            if (ep != null) {
                serverHost = ep.host();
                serverPort = ep.port();
            }
        }

        viewPager = findViewById(R.id.mainViewPager);
        bottomNav = findViewById(R.id.mainBottomNav);
        ViewCompat.setOnApplyWindowInsetsListener(
                bottomNav,
                (v, windowInsets) -> {
                    int gesture = windowInsets.getInsets(WindowInsetsCompat.Type.navigationBars()).bottom;
                    v.setPadding(v.getPaddingLeft(), v.getPaddingTop(), v.getPaddingRight(), gesture);
                    return windowInsets;
                });

        viewPager.setAdapter(new MainTabPagerAdapter(this, serverHost, serverPort));
        viewPager.setOffscreenPageLimit(2);
        TabPageTransforms.apply(viewPager, 8f);

        viewPager.registerOnPageChangeCallback(
                new ViewPager2.OnPageChangeCallback() {
                    @Override
                    public void onPageSelected(int position) {
                        if (position >= 0 && position < BOTTOM_NAV_IDS.length) {
                            int want = BOTTOM_NAV_IDS[position];
                            if (bottomNav.getSelectedItemId() != want) {
                                bottomNav.setSelectedItemId(want);
                            }
                        }
                    }
                });

        bottomNav.setOnItemSelectedListener(
                item -> {
                    int id = item.getItemId();
                    int pos = indexForNavId(id);
                    if (viewPager.getCurrentItem() != pos) {
                        viewPager.setCurrentItem(pos, true);
                    }
                    return true;
                });

        handleOpenCommunicationTab(getIntent());
        handleNotificationIntent(getIntent());

        PushTokenRegistration.registerIfSignedIn(this);
    }

    @Nullable
    private ServerEndpoint resolveServerEndpoint() {
        String host = getIntent().getStringExtra(EXTRA_HOST);
        int port = getIntent().getIntExtra(EXTRA_PORT, 0);
        if (host != null && !host.isBlank() && port > 0) {
            return new ServerEndpoint(host, port);
        }
        return new ServerConfigStore(this).getSavedEndpoint();
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        handleOpenCommunicationTab(intent);
        handleNotificationIntent(intent);
    }

    @Override
    protected void onResume() {
        super.onResume();
        maybeRequestPostNotificationsPermission();
        refreshFriendRequestTabBadgeAsync();
        mainHandler.removeCallbacks(openSessionsSyncRunnable);
        mainHandler.post(openSessionsSyncRunnable);
    }

    @Override
    protected void onPause() {
        mainHandler.removeCallbacks(openSessionsSyncRunnable);
        super.onPause();
    }

    private void maybeRequestPostNotificationsPermission() {
        if (Build.VERSION.SDK_INT < 33) {
            return;
        }
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.POST_NOTIFICATIONS)
                == PackageManager.PERMISSION_GRANTED) {
            return;
        }
        ActivityCompat.requestPermissions(
                this, new String[] {Manifest.permission.POST_NOTIFICATIONS}, REQ_POST_NOTIFICATIONS);
    }

    private void handleOpenCommunicationTab(@Nullable Intent intent) {
        if (intent == null || !intent.getBooleanExtra(EXTRA_OPEN_COMMUNICATION_TAB, false)) {
            return;
        }
        if (viewPager != null) {
            viewPager.setCurrentItem(0, false);
        }
        if (bottomNav != null) {
            bottomNav.setSelectedItemId(R.id.nav_communication);
        }
    }

    private void handleNotificationIntent(@Nullable Intent intent) {
        if (intent == null || !intent.getBooleanExtra(NotificationNavExtras.EXTRA_FROM_NOTIFICATION, false)) {
            return;
        }
        int nav = intent.getIntExtra(NotificationNavExtras.EXTRA_NAV_TARGET, NotificationNavExtras.NAV_NONE);
        if (nav == NotificationNavExtras.NAV_NONE) {
            return;
        }
        View root = findViewById(R.id.mainRoot);
        if (root == null) {
            return;
        }
        root.post(
                () -> {
                    if (isFinishing()) {
                        return;
                    }
                    if (serverHost == null || serverHost.isBlank() || serverPort <= 0) {
                        return;
                    }
                    Intent i = intent;
                    switch (nav) {
                        case NotificationNavExtras.NAV_OPEN_CHAT: {
                            String peer = i.getStringExtra(ChatActivity.EXTRA_PEER_USER_ID_HEX);
                            String name = i.getStringExtra(ChatActivity.EXTRA_PEER_DISPLAY_NAME);
                            if (peer != null && peer.length() == 32) {
                                startActivity(
                                        ChatActivity.buildIntent(
                                                MainPlaceholderActivity.this,
                                                serverHost,
                                                serverPort,
                                                peer,
                                                name != null ? name : ""));
                            }
                            break;
                        }
                        case NotificationNavExtras.NAV_FRIEND_REQUESTS: {
                            Intent fr = new Intent(MainPlaceholderActivity.this, FriendRequestsActivity.class);
                            fr.putExtra(FriendRequestsActivity.EXTRA_HOST, serverHost);
                            fr.putExtra(FriendRequestsActivity.EXTRA_PORT, serverPort);
                            startActivity(fr);
                            break;
                        }
                        case NotificationNavExtras.NAV_FRIEND_REMOVED:
                            if (viewPager != null) {
                                viewPager.setCurrentItem(1, false);
                            }
                            if (bottomNav != null) {
                                bottomNav.setSelectedItemId(R.id.nav_contacts);
                            }
                            break;
                        default:
                            break;
                    }
                    i.removeExtra(NotificationNavExtras.EXTRA_FROM_NOTIFICATION);
                    i.removeExtra(NotificationNavExtras.EXTRA_NAV_TARGET);
                });
    }

    /** 与 {@link FeaturesFragment} 同步底部「功能」角标，避免仅切换 Tab 时数字不一致。 */
    public void syncFriendRequestTabBadge(int pendingCount) {
        runOnUiThread(() -> applyFriendRequestTabBadge(pendingCount));
    }

    private void refreshFriendRequestTabBadgeAsync() {
        if (serverHost == null || serverHost.isBlank() || serverPort <= 0) {
            return;
        }
        badgeExecutor.execute(
                () -> {
                    int n = FriendPendingRequests.fetchPendingCount(this, serverHost, serverPort);
                    runOnUiThread(
                            () -> {
                                applyFriendRequestTabBadge(n);
                                FriendRequestState.setLastKnownPendingCount(MainPlaceholderActivity.this, n);
                            });
                });
    }

    private void applyFriendRequestTabBadge(int count) {
        if (isFinishing() || bottomNav == null) {
            return;
        }
        if (count <= 0) {
            bottomNav.removeBadge(R.id.nav_features);
        } else {
            BadgeDrawable b = bottomNav.getOrCreateBadge(R.id.nav_features);
            b.setVisible(true);
            b.setNumber(Math.min(count, 999));
        }
    }

    @Override
    protected void onDestroy() {
        mainHandler.removeCallbacks(openSessionsSyncRunnable);
        super.onDestroy();
        badgeExecutor.shutdownNow();
    }

    private static int indexForNavId(int itemId) {
        if (itemId == R.id.nav_contacts) {
            return 1;
        }
        if (itemId == R.id.nav_features) {
            return 2;
        }
        if (itemId == R.id.nav_profile) {
            return 3;
        }
        return 0;
    }
}
