package com.ztrust.zchat.im.zsp;

public final class ZspConstants {

    public static final int MAGIC = 0x5A53;
    public static final int PROTOCOL_VERSION = 0x01;
    public static final int HEADER_LENGTH = 16;
    public static final int AUTH_TAG_LENGTH = 16;
    public static final int MAX_META_LENGTH = 4096;
    public static final int MAX_PAYLOAD_LENGTH_U16 = 65_535;
    public static final int MAX_TOKEN_LENGTH = 4096;
    public static final int MIN_META_LENGTH = 26;

    private ZspConstants() {}
}
