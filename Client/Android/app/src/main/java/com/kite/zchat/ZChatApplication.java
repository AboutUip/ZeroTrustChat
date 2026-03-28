package com.kite.zchat;

import android.app.Application;

import com.kite.zchat.call.VoiceCallEngine;
import com.kite.zchat.chat.ChatSync;
import com.kite.zchat.core.ServerConfigStore;
import com.kite.zchat.core.ServerEndpoint;
import com.kite.zchat.push.FriendRequestPollWorker;
import com.kite.zchat.push.IncomingPushNotifier;
import com.kite.zchat.push.VoiceCallNotificationHelper;
import com.kite.zchat.push.ZChatNotificationHelper;
import com.kite.zchat.zsp.ZspSessionManager;

import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public final class ZChatApplication extends Application {

    /** 与 ZSP 入站 TEXT 分发同线程，避免与 UI 线程互相阻塞。 */
    private final ExecutorService zspIncomingExecutor = Executors.newSingleThreadExecutor();

    @Override
    public void onCreate() {
        super.onCreate();
        // WebRTC 在首次语音通话时于 {@link WebRtcPeerConnectionFactoryHolder} 懒加载，避免错误 AAR/缺类拖垮进程启动。

        ZChatNotificationHelper.createChannels(this);
        VoiceCallNotificationHelper.createChannel(this);
        FriendRequestPollWorker.schedule(this);

        ZspSessionManager.get()
                .setIncomingTextListener(
                        payload ->
                                zspIncomingExecutor.execute(
                                        () -> {
                                            ServerEndpoint ep =
                                                    new ServerConfigStore(ZChatApplication.this)
                                                            .getSavedEndpoint();
                                            if (ep == null) {
                                                return;
                                            }
                                            VoiceCallEngine.dispatchSignalingFromIncomingTextPayload(
                                                    getApplicationContext(), payload);
                                            ChatSync.scheduleSyncFromIncomingPush(
                                                    getApplicationContext(),
                                                    ep.host(),
                                                    ep.port(),
                                                    payload);
                                            IncomingPushNotifier.onIncomingTextSynced(
                                                    getApplicationContext(),
                                                    ep.host(),
                                                    ep.port(),
                                                    payload);
                                        }));
    }
}
