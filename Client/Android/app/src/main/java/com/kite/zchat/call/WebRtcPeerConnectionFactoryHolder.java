package com.kite.zchat.call;

import android.content.Context;

import org.webrtc.DefaultVideoDecoderFactory;
import org.webrtc.DefaultVideoEncoderFactory;
import org.webrtc.EglBase;
import org.webrtc.PeerConnectionFactory;

/**
 * 进程内单例 {@link PeerConnectionFactory}（音视频工厂；本应用仅使用音频轨）。
 */
public final class WebRtcPeerConnectionFactoryHolder {

    private static final Object LOCK = new Object();
    private static PeerConnectionFactory factory;
    private static EglBase eglBase;

    private WebRtcPeerConnectionFactoryHolder() {}

    public static PeerConnectionFactory get(Context context) {
        synchronized (LOCK) {
            if (factory != null) {
                return factory;
            }
            Context app = context.getApplicationContext();
            PeerConnectionFactory.initialize(
                    PeerConnectionFactory.InitializationOptions.builder(app)
                            .setEnableInternalTracer(false)
                            .createInitializationOptions());
            eglBase = EglBase.create();
            DefaultVideoEncoderFactory enc =
                    new DefaultVideoEncoderFactory(
                            eglBase.getEglBaseContext(), true, true);
            DefaultVideoDecoderFactory dec =
                    new DefaultVideoDecoderFactory(eglBase.getEglBaseContext());
            factory =
                    PeerConnectionFactory.builder()
                            .setVideoEncoderFactory(enc)
                            .setVideoDecoderFactory(dec)
                            .createPeerConnectionFactory();
            return factory;
        }
    }
}
