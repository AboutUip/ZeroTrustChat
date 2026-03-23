package com.ztrust.zchat.im.zsp.server;

public interface ZspJniBridge {

    byte[] auth(byte[] userId, byte[] token, byte[] clientIp);

    void destroyCallerSession(byte[] callerSessionId);

    void touchSession(byte[] callerSessionId, byte[] imSessionId, long nowMs);
}
