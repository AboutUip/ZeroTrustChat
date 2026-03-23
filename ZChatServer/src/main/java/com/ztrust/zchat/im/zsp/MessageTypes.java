package com.ztrust.zchat.im.zsp;

public final class MessageTypes {

    public static final int TEXT = 0x01;
    public static final int IMAGE = 0x02;
    public static final int VOICE = 0x03;
    public static final int VIDEO = 0x04;
    public static final int FILE_INFO = 0x05;
    public static final int FILE_CHUNK = 0x06;
    public static final int FILE_COMPLETE = 0x07;
    public static final int VOICE_CALL = 0x08;
    public static final int VIDEO_CALL = 0x09;
    public static final int CALL_SIGNAL = 0x0A;
    public static final int TYPING = 0x0B;
    public static final int RECEIPT = 0x0C;
    public static final int ACK = 0x0D;
    public static final int GROUP_INVITE = 0x0E;
    public static final int GROUP_CREATE = 0x0F;
    public static final int GROUP_UPDATE = 0x10;
    public static final int GROUP_LEAVE = 0x11;
    public static final int FRIEND_REQUEST = 0x12;
    public static final int FRIEND_RESPONSE = 0x13;
    public static final int GROUP_MUTE = 0x14;
    public static final int GROUP_REMOVE = 0x15;
    public static final int GROUP_TRANSFER_OWNER = 0x16;
    public static final int GROUP_JOIN_REQUEST = 0x17;
    public static final int DELETE_FRIEND = 0x18;
    public static final int FRIEND_NOTE_UPDATE = 0x19;
    public static final int RESUME_TRANSFER = 0x1A;
    public static final int CANCEL_TRANSFER = 0x1B;
    public static final int GROUP_NAME_UPDATE = 0x1C;
    public static final int HEARTBEAT = 0x80;
    public static final int AUTH = 0x81;
    public static final int LOGOUT = 0x82;
    public static final int SYNC = 0x83;
    public static final int CUSTOM = 0xFE;

    private MessageTypes() {}
}
