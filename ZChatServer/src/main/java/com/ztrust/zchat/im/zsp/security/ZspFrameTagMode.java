package com.ztrust.zchat.im.zsp.security;

/**
 * Auth Tag 字段策略。协议第九节为 AEAD 输出；网关在 Java 不持 E2E 密钥时可用 HMAC 作为帧完整性替代，须与客户端一致。
 */
public enum ZspFrameTagMode {
    /** 16 字节全零（开发/或依赖 TLS）。 */
    NONE,
    /**
     * HMAC-SHA256(header16 ‖ meta ‖ payload) 截断为 16 字节；与 AES-GCM Auth Tag 算法不同，仅作传输完整性。
     */
    HMAC_SHA256_128
}
