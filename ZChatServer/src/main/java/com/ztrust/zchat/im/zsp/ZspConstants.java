package com.ztrust.zchat.im.zsp;

public final class ZspConstants {

    public static final int MAGIC = 0x5A53;
    public static final int PROTOCOL_VERSION = 0x01;
    public static final int HEADER_LENGTH = 16;
    public static final int AUTH_TAG_LENGTH = 16;
    public static final int MAX_META_LENGTH = 4096;
    /** Header Length 字段为 uint16；单帧 Payload 上限与此一致。 */
    public static final int MAX_PAYLOAD_LENGTH_U16 = 65_535;
    public static final int MAX_TOKEN_LENGTH = 4096;
    public static final int MIN_META_LENGTH = 26;
    /** JNI / MM2 侧 userId、imSessionId、messageId、groupId 等固定 16 字节（见 docs/06-Appendix/01-JNI.md）。 */
    public static final int USER_ID_SIZE = 16;
    /** ZSP Header 中 SessionID 字段为 4 字节，与 imSessionId（16B）不同。 */
    public static final int ZSP_HEADER_SESSION_ID_SIZE = 4;
    public static final int ED25519_PUBLIC_KEY_SIZE = 32;
    public static final int ED25519_SIGNATURE_SIZE = 64;
    public static final int SHA256_SIZE = 32;

    /** mm1_user_kv，与 {@code Types.h} 中 MM1_USER_KV_TYPE_AVATAR_V1 一致（ASCII "AVT1"）。 */
    public static final int MM1_USER_KV_TYPE_AVATAR_V1 = 0x41565431;
    /** 头像 blob 最大字节数（与 ZSP 单帧载荷上限一致）。 */
    public static final int MM1_USER_AVATAR_MAX_BYTES = 65535;

    /** mm1_user_kv，与 {@code Types.h} 中 MM1_USER_KV_TYPE_DISPLAY_NAME_V1 一致（ASCII "NMN1"）。 */
    public static final int MM1_USER_KV_TYPE_DISPLAY_NAME_V1 = 0x4E4D4E31;
    /** UTF-8 昵称最大字节数。 */
    public static final int MM1_USER_DISPLAY_NAME_MAX_BYTES = 256;
    /** mm1_user_kv，Ed25519 身份公钥（ASCII "EDJ1"），与 FriendVerificationManager 一致。 */
    public static final int MM1_USER_KV_TYPE_ED25519_PUBKEY_V1 = 0x45444A31;

    private ZspConstants() {}
}
