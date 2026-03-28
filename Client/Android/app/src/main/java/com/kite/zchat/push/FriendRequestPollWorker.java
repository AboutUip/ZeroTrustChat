package com.kite.zchat.push;

import android.content.Context;

import androidx.annotation.NonNull;
import androidx.work.Constraints;
import androidx.work.ExistingPeriodicWorkPolicy;
import androidx.work.NetworkType;
import androidx.work.PeriodicWorkRequest;
import androidx.work.WorkManager;
import androidx.work.Worker;
import androidx.work.WorkerParameters;

import com.kite.zchat.auth.AuthCredentialStore;
import com.kite.zchat.core.ServerConfigStore;
import com.kite.zchat.core.ServerEndpoint;
import com.kite.zchat.friends.FriendPendingRequests;

import java.util.concurrent.TimeUnit;

/** 后台定期拉取待处理好友申请数量；进程无长连接时作为补充（真正即时仍依赖 FCM）。 */
public final class FriendRequestPollWorker extends Worker {

    private static final String UNIQUE_NAME = "zchat_friend_request_poll";

    public FriendRequestPollWorker(@NonNull Context context, @NonNull WorkerParameters workerParams) {
        super(context, workerParams);
    }

    public static void schedule(Context context) {
        Constraints c =
                new Constraints.Builder()
                        .setRequiredNetworkType(NetworkType.CONNECTED)
                        .build();
        PeriodicWorkRequest req =
                new PeriodicWorkRequest.Builder(FriendRequestPollWorker.class, 15, TimeUnit.MINUTES)
                        .setConstraints(c)
                        .build();
        WorkManager.getInstance(context)
                .enqueueUniquePeriodicWork(UNIQUE_NAME, ExistingPeriodicWorkPolicy.KEEP, req);
    }

    @NonNull
    @Override
    public Result doWork() {
        Context app = getApplicationContext();
        if (!AuthCredentialStore.create(app).hasCredentials()) {
            return Result.success();
        }
        ServerEndpoint ep = new ServerConfigStore(app).getSavedEndpoint();
        if (ep == null) {
            return Result.success();
        }
        int count = FriendPendingRequests.fetchPendingCount(app, ep.host(), ep.port());
        int last = FriendRequestState.getLastKnownPendingCount(app);
        if (last < 0) {
            FriendRequestState.setLastKnownPendingCount(app, count);
            return Result.success();
        }
        if (count > last) {
            ZChatNotificationHelper.showFriendRequestNotification(app, ep.host(), ep.port(), count);
            FriendRequestState.setLastKnownPendingCount(app, count);
        }
        return Result.success();
    }
}
