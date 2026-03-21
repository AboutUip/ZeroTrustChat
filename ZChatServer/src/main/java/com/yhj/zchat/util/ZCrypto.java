package com.yhj.zchat.util;

import javax.crypto.Cipher;
import javax.crypto.spec.GCMParameterSpec;
import javax.crypto.spec.SecretKeySpec;
import java.nio.charset.StandardCharsets;
import java.util.HexFormat;

/**
 * ZSP 协议解密工具
 * 使用 AES-256-GCM 算法
 */
public class ZCrypto {

    private static final String ALGORITHM = "AES/GCM/NoPadding";
    private static final int GCM_TAG_LENGTH = 128; // 16字节 = 128位

    /**
     * 解密 Payload
     *
     * @param key      32字节 AES-256 密钥
     * @param nonce    12字节随机数
     * @param payload  加密的消息体
     * @param authTag  16字节认证标签
     * @return 解密后的明文
     */
    public static byte[] decrypt(byte[] key, byte[] nonce, byte[] payload, byte[] authTag) throws Exception {
        // AES-GCM 需要 ciphertext + authTag 拼在一起
        byte[] ciphertext = new byte[payload.length + authTag.length];
        System.arraycopy(payload, 0, ciphertext, 0, payload.length);
        System.arraycopy(authTag, 0, ciphertext, payload.length, authTag.length);

        SecretKeySpec keySpec = new SecretKeySpec(key, "AES");
        GCMParameterSpec gcmSpec = new GCMParameterSpec(GCM_TAG_LENGTH, nonce);

        Cipher cipher = Cipher.getInstance(ALGORITHM);
        cipher.init(Cipher.DECRYPT_MODE, keySpec, gcmSpec);

        return cipher.doFinal(ciphertext);
    }

    /**
     * 加密 Payload（用于测试）
     *
     * @param key      32字节 AES-256 密钥
     * @param nonce    12字节随机数
     * @param plaintext 明文
     * @return ciphertext + authTag 拼接结果
     */
    public static byte[] encrypt(byte[] key, byte[] nonce, byte[] plaintext) throws Exception {
        SecretKeySpec keySpec = new SecretKeySpec(key, "AES");
        GCMParameterSpec gcmSpec = new GCMParameterSpec(GCM_TAG_LENGTH, nonce);

        Cipher cipher = Cipher.getInstance(ALGORITHM);
        cipher.init(Cipher.ENCRYPT_MODE, keySpec, gcmSpec);

        return cipher.doFinal(plaintext);
    }

    // 测试
    public static void main(String[] args) throws Exception {
        // ========== 测试：生成一个完整的数据包 ==========

        // 1. 假设密钥是 32 字节的测试密钥（实际应该从密钥管理获取）
        byte[] testKey = new byte[32]; // 全0测试密钥
        for (int i = 0; i < 32; i++) testKey[i] = (byte) i; // 填充 0-31

        // 2. 12 字节 Nonce
        byte[] nonce = new byte[12];
        for (int i = 0; i < 12; i++) nonce[i] = (byte) i;

        // 3. 明文消息
        String message = "Hello World";
        byte[] plaintext = message.getBytes(StandardCharsets.UTF_8);

        System.out.println("=== 加密测试 ===");
        System.out.println("明文: " + message);
        System.out.println("明文16进制: " + HexFormat.of().formatHex(plaintext));

        // 4. 加密
        byte[] encrypted = encrypt(testKey, nonce, plaintext);
        System.out.println("密文+AuthTag: " + HexFormat.of().formatHex(encrypted));
        System.out.println("密文长度: " + (encrypted.length - 16) + " 字节");
        System.out.println("AuthTag: " + HexFormat.of().formatHex(encrypted, encrypted.length - 16, 16));

        // 5. 分离 ciphertext 和 authTag
        int ciphertextLen = encrypted.length - 16;
        byte[] ciphertext = new byte[ciphertextLen];
        byte[] authTag = new byte[16];
        System.arraycopy(encrypted, 0, ciphertext, 0, ciphertextLen);
        System.arraycopy(encrypted, ciphertextLen, authTag, 0, 16);

        // 6. 解密
        System.out.println("\n=== 解密测试 ===");
        byte[] decrypted = decrypt(testKey, nonce, ciphertext, authTag);
        System.out.println("解密结果: " + new String(decrypted, StandardCharsets.UTF_8));
    }
}
