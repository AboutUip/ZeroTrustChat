package com.ztrust.zchat.im.zsp.server;

final class NoopZspJniBridge implements ZspJniBridge {

    @Override
    public byte[] auth(byte[] userId, byte[] token, byte[] clientIp) {
        return null;
    }

    @Override
    public void destroyCallerSession(byte[] callerSessionId) {
    }

    @Override
    public void touchSession(byte[] callerSessionId, byte[] imSessionId, long nowMs) {
    }
}
