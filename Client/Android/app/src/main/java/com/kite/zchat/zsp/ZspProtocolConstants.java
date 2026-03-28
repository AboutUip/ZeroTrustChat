package com.kite.zchat.zsp;

/** 与 ZChatServer {@code MessageTypes} / {@code ZspConstants} 对齐。 */
public final class ZspProtocolConstants {

    public static final int MAGIC = 0x5A53;
    public static final int PROTOCOL_VERSION = 0x01;
    public static final int HEADER_LENGTH = 16;
    public static final int AUTH_TAG_LENGTH = 16;
    public static final int MIN_META_LENGTH = 26;
    public static final int USER_ID_SIZE = 16;

    public static final int MESSAGE_TEXT = 0x01;
    /** 会话消息增量拉取（应答为 count‖rows）。 */
    public static final int SYNC = 0x83;
    public static final int HEARTBEAT = 0x80;
    public static final int LOCAL_REGISTER = 0x84;
    public static final int LOCAL_PASSWORD_AUTH = 0x85;

    public static final int USER_AVATAR_SET = 0x86;
    public static final int USER_AVATAR_GET = 0x87;
    public static final int USER_AVATAR_DELETE = 0x88;
    public static final int USER_DISPLAY_NAME_SET = 0x8A;
    public static final int USER_DISPLAY_NAME_GET = 0x8B;
    public static final int USER_PROFILE_GET = 0x8C;
    /** 注销账号：载荷为 SHA-256(UTF-8 口令) 共 32 字节；应答 1 字节。 */
    public static final int ACCOUNT_DELETE = 0x8D;
    /** 好友列表：请求载荷为空；应答 count(u32 BE) ‖ count×userId(16)。 */
    public static final int FRIEND_LIST_GET = 0x8E;
    /** 群 ID 列表：请求载荷为空；应答 count(u32 BE) ‖ count×groupId(16)。 */
    public static final int GROUP_LIST_GET = 0x8F;
    /** 群信息：请求 groupId(16)；应答 nameLen‖name UTF-8‖memberCount‖members×16B。 */
    public static final int GROUP_INFO_GET = 0x90;
    /** 好友资料：与 USER_PROFILE_GET 相同。 */
    public static final int FRIEND_INFO_GET = 0x91;
    /** 发布本人 Ed25519 公钥（UserData EDJ1）：载荷 32 字节；应答 1 字节。 */
    public static final int IDENTITY_ED25519_PUBLISH = 0x92;
    /** 待处理好友申请列表：请求空；应答见 {@link ZspFriendCodec}。 */
    public static final int FRIEND_PENDING_LIST_GET = 0x93;

    public static final int FRIEND_REQUEST = 0x12;
    public static final int FRIEND_RESPONSE = 0x13;
    /** 删除好友：载荷 userId(16)‖friendId(16)‖timestamp(u64 BE)‖signatureEd25519(64)；应答 1 字节 1=成功。 */
    public static final int DELETE_FRIEND = 0x18;

    public static final int MM1_USER_KV_TYPE_AVATAR_V1 = 0x41565431;
    public static final int MM1_USER_AVATAR_MAX_BYTES = 65535;
    public static final int MM1_USER_KV_TYPE_DISPLAY_NAME_V1 = 0x4E4D4E31;
    public static final int MM1_USER_DISPLAY_NAME_MAX_BYTES = 256;
    /** 与 {@code FriendVerificationManager} / EDJ1 一致。 */
    public static final int MM1_USER_KV_TYPE_ED25519_PUBKEY_V1 = 0x45444A31;

    public static final int ED25519_PUBLIC_KEY_SIZE = 32;
    public static final int ED25519_SIGNATURE_SIZE = 64;

    private ZspProtocolConstants() {}
}
