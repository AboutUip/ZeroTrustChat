package com.ztrust.zchat.im.zsp.server;

import com.ztrust.zchat.im.jni.ZChatIMNative;

final class ZChatIMNativeJniBridge implements ZspJniBridge {

    ZChatIMNativeJniBridge(ZspServerProperties props) {
        String data = props.getNative().getDataDir();
        String index = props.getNative().getIndexDir();
        if (data == null || index == null) {
            throw new IllegalStateException("zchat.zsp.native.data-dir and index-dir are required when jni-enabled=true");
        }
        if (!ZChatIMNative.initialize(data, index)) {
            throw new IllegalStateException("ZChatIMNative.initialize returned false");
        }
    }

    @Override
    public byte[] auth(byte[] userId, byte[] token, byte[] clientIp) {
        return ZChatIMNative.auth(userId, token, clientIp);
    }

    @Override
    public void destroyCallerSession(byte[] callerSessionId) {
        ZChatIMNative.destroySession(callerSessionId, callerSessionId);
    }

    @Override
    public void touchSession(byte[] callerSessionId, byte[] imSessionId, long nowMs) {
        ZChatIMNative.touchSession(callerSessionId, imSessionId, nowMs);
    }
}
