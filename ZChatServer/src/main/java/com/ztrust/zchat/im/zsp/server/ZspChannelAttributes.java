package com.ztrust.zchat.im.zsp.server;

import io.netty.util.AttributeKey;

public final class ZspChannelAttributes {

    public static final AttributeKey<byte[]> CALLER_SESSION_ID =
            AttributeKey.valueOf("zchatZspCallerSessionId");

    public static final AttributeKey<Long> OUTBOUND_SEQUENCE =
            AttributeKey.valueOf("zchatZspOutboundSequence");

    private ZspChannelAttributes() {}
}
