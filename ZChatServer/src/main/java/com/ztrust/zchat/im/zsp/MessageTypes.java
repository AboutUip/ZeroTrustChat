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
    /** 本地口令开户（载荷：userId16 | pwLen u16 BE | password UTF-8 | recLen u16 BE | recovery UTF-8），应答 1 字节：1=成功，0=失败。 */
    public static final int LOCAL_REGISTER = 0x84;
    /** 本地口令登录（载荷：userId16 | pwLen u16 BE | password UTF-8），成功时载荷为 imSessionId（与 AUTH 一致），失败时载荷长度为 0。 */
    public static final int LOCAL_PASSWORD_AUTH = 0x85;
    /** 设置本人头像：载荷为原图字节（≤ {@link com.ztrust.zchat.im.zsp.ZspConstants#MM1_USER_AVATAR_MAX_BYTES}）；应答 1 字节 1=成功。 */
    public static final int USER_AVATAR_SET = 0x86;
    /** 读取头像：载荷为目标 userId(16)；应答为原图字节，无权限或无数据时载荷为空。 */
    public static final int USER_AVATAR_GET = 0x87;
    /** 删除本人头像：载荷可为空；应答 1 字节 1=成功。 */
    public static final int USER_AVATAR_DELETE = 0x88;
    /** 设置本人昵称：载荷为 UTF-8 字节（≤ {@link com.ztrust.zchat.im.zsp.ZspConstants#MM1_USER_DISPLAY_NAME_MAX_BYTES}）；应答 1 字节。 */
    public static final int USER_DISPLAY_NAME_SET = 0x8A;
    /** 读取昵称：载荷为目标 userId(16)；应答为 UTF-8 或空。 */
    public static final int USER_DISPLAY_NAME_GET = 0x8B;
    /** 读取用户资料（昵称+头像）：载荷为目标 userId(16)；应答见 02-ZSP-Protocol.md §6.9。 */
    public static final int USER_PROFILE_GET = 0x8C;
    /**
     * 注销当前账号（JNI {@code deleteAccount}）：载荷为 SHA-256(UTF-8 口令) 32 字节；应答 1 字节 1=成功。成功后网关关闭连接。
     */
    public static final int ACCOUNT_DELETE = 0x8D;
    /**
     * 好友列表：拉取当前登录用户的已接受好友 userId（16B）列表。请求载荷为空；应答：{@code count(u32 BE) ‖ count×userId(16)}。
     */
    public static final int FRIEND_LIST_GET = 0x8E;
    /**
     * 群 ID 列表：拉取当前用户所加入的群 groupId（16B）。请求载荷为空；应答：{@code count(u32 BE) ‖ count×groupId(16)}。
     * 底层 JNI 未提供枚举时由网关返回 count=0（占位）。
     */
    public static final int GROUP_LIST_GET = 0x8F;
    /**
     * 群聊信息：群名 + 成员 userId 列表。请求载荷：{@code groupId(16)}；应答：{@code nameLen(u16 BE) ‖ name UTF-8 ‖ memberCount(u32 BE) ‖ memberCount×userId(16)}。
     */
    public static final int GROUP_INFO_GET = 0x90;
    /**
     * 好友资料（与 {@link #USER_PROFILE_GET} 载荷/应答相同）：{@code targetUserId(16)}；应答为昵称长度+昵称+头像长度+头像。
     */
    public static final int FRIEND_INFO_GET = 0x91;
    /**
     * 发布本人 Ed25519 公钥（UserData EDJ1）：载荷为 32 字节公钥；应答 1 字节 1=成功。
     */
    public static final int IDENTITY_ED25519_PUBLISH = 0x92;
    /**
     * 待处理好友申请列表：请求载荷为空；应答 count(u32 BE) ‖ count×(requestId(16)‖fromUserId(16)‖createdSec(u64 BE))。
     */
    public static final int FRIEND_PENDING_LIST_GET = 0x93;
    public static final int CUSTOM = 0xFE;

    private MessageTypes() {}
}
