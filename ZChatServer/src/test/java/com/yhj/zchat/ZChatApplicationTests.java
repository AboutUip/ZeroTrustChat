package com.yhj.zchat;

import org.junit.jupiter.api.Test;
import org.springframework.boot.test.context.SpringBootTest;

import java.nio.charset.StandardCharsets;
import java.util.HexFormat;

@SpringBootTest
class ZChatApplicationTests {

    /**
     * 组长发来的测试数据
     *
     * 数据分析：
     * - 总长度: 52 字节
     * - Header: 16 字节
     * - Meta: 26 字节 (MetaLength=24 不含自身2字节)
     * - Payload: 10 字节 (加密数据)
     * - AuthTag: 缺失 (正常需要16字节)
     *
     * 结论：这是测试数据，Payload是加密的，AuthTag被省略
     */
    private static final String ZSP_HEX =
            "5a53010102000000000100000001000b00180000018bcfe568000000000000000000000000010000000100eeb878b077c4ca3baba0f07c6a9ddc30e6487735b2237bbde99c";

    @Test
    void decodeZSPPacket() {
        byte[] data = HexFormat.of().parseHex(ZSP_HEX);
        System.out.println("\n╔════════════════════════════════════════╗");
        System.out.println("║       ZSP 协议数据包解析报告            ║");
        System.out.println("╚════════════════════════════════════════╝");
        System.out.println("数据包总长度: " + data.length + " 字节\n");

        int index = 0;

        // ========== Header ==========
        System.out.println("┌─────────────────────────────────────────┐");
        System.out.println("│ Header (协议头) - 16 字节               │");
        System.out.println("├─────────────────────────────────────────┤");

        short magic = getShort(data, index);
        boolean magicValid = (magic == 0x5A53);
        System.out.printf("│ [0-1]   Magic:     0x%04X %s\n", magic & 0xFFFF, magicValid ? "✓ 正确" : "✗ 错误");
        index += 2;

        byte version = data[index++];
        System.out.printf("│ [2]     Version:   %d\n", version);

        byte msgType = data[index++];
        System.out.printf("│ [3]     MsgType:   0x%02X (%s)\n", msgType & 0xFF, getMessageTypeName(msgType));

        byte flags = data[index++];
        System.out.printf("│ [4]     Flags:     0x%02X (%s)\n", flags & 0xFF, decodeFlags(flags));

        byte reserved = data[index++];
        System.out.printf("│ [5]     Reserved:  0x%02X\n", reserved & 0xFF);

        int sessionId = getInt(data, index);
        System.out.printf("│ [6-9]   SessionID: %d\n", sessionId);
        index += 4;

        int sequence = getInt(data, index);
        System.out.printf("│ [10-13] Sequence:  %d\n", sequence);
        index += 4;

        short payloadLen = getShort(data, index);
        System.out.printf("│ [14-15] Length:    %d\n", payloadLen);
        index += 2;

        System.out.println("└─────────────────────────────────────────┘\n");

        // ========== Meta ==========
        System.out.println("┌─────────────────────────────────────────┐");
        System.out.println("│ Meta (元数据) - 变长                    │");
        System.out.println("├─────────────────────────────────────────┤");

        short metaLength = getShort(data, index);
        System.out.printf("│ [0-1]   MetaLength: %d (不含自身, 实际%d字节)\n", metaLength, metaLength + 2);
        index += 2;

        long timestamp = getLong(data, index);
        java.util.Date date = new java.util.Date(timestamp);
        System.out.printf("│ [2-9]   Timestamp:  %d\n", timestamp);
        System.out.printf("│         时间:       %tF %tT\n", date, date);
        index += 8;

        byte[] nonce = new byte[12];
        copyBytes(data, index, nonce);
        System.out.printf("│ [10-21] Nonce:      %s\n", HexFormat.of().formatHex(nonce));
        index += 12;

        int keyId = getInt(data, index);
        System.out.printf("│ [22-25] KeyID:      %d\n", keyId);
        index += 4;

        System.out.println("└─────────────────────────────────────────┘\n");

        // ========== Payload ==========
        int remaining = data.length - index;
        System.out.println("┌─────────────────────────────────────────┐");
        System.out.println("│ Payload (消息体)                        │");
        System.out.println("├─────────────────────────────────────────┤");
        System.out.printf("│ Header.Length: %d 字节\n", payloadLen);
        System.out.printf("│ 剩余字节数:    %d 字节\n", remaining);
        System.out.printf("│ AuthTag需求:   16 字节\n");

        // 实际Payload = 剩余 - AuthTag(16)
        // 但数据不够16字节AuthTag，说明AuthTag被省略了
        int actualPayloadLen = remaining;
        System.out.printf("│ 实际Payload:   %d 字节 (AuthTag已省略)\n", actualPayloadLen);

        byte[] payload = new byte[actualPayloadLen];
        copyBytes(data, index, payload);

        System.out.println("├─────────────────────────────────────────┤");
        System.out.println("│ 16进制:");
        System.out.printf("│   %s\n", HexFormat.of().formatHex(payload));
        System.out.println("│ UTF-8解码:");
        System.out.printf("│   %s (加密数据，乱码正常)\n", new String(payload, StandardCharsets.UTF_8));
        System.out.println("└─────────────────────────────────────────┘\n");

        // ========== 总结 ==========
        System.out.println("╔════════════════════════════════════════╗");
        System.out.println("║               解析总结                  ║");
        System.out.println("╠════════════════════════════════════════╣");
        System.out.printf("║ 协议版本:  %d\n", version);
        System.out.printf("║ 消息类型:  %s\n", getMessageTypeName(msgType));
        System.out.printf("║ 加密状态:  %s\n", (flags & 0x02) != 0 ? "已加密 ✓" : "未加密");
        System.out.printf("║ 会话ID:    %d\n", sessionId);
        System.out.printf("║ 序列号:    %d\n", sequence);
        System.out.printf("║ 密钥ID:    %d\n", keyId);
        System.out.println("║                                        ║");
        System.out.println("║      Payload乱码是因为数据已加密      ║");
        System.out.println("║       需要用KeyID对应的密钥解密        ║");
        System.out.println("╚════════════════════════════════════════╝\n");
    }

    // ========== 工具方法 ==========

    private short getShort(byte[] b, int i) {
        return (short) (((b[i] & 0xFF) << 8) | (b[i + 1] & 0xFF));
    }

    private int getInt(byte[] b, int i) {
        return ((b[i] & 0xFF) << 24) | ((b[i + 1] & 0xFF) << 16) |
               ((b[i + 2] & 0xFF) << 8) | (b[i + 3] & 0xFF);
    }

    private long getLong(byte[] b, int i) {
        return ((long) (b[i] & 0xFF) << 56) | ((long) (b[i + 1] & 0xFF) << 48) |
               ((long) (b[i + 2] & 0xFF) << 40) | ((long) (b[i + 3] & 0xFF) << 32) |
               ((long) (b[i + 4] & 0xFF) << 24) | ((long) (b[i + 5] & 0xFF) << 16) |
               ((long) (b[i + 6] & 0xFF) << 8) | (long) (b[i + 7] & 0xFF);
    }

    private void copyBytes(byte[] src, int pos, byte[] dest) {
        System.arraycopy(src, pos, dest, 0, dest.length);
    }

    private String getMessageTypeName(byte type) {
        return switch (type & 0xFF) {
            case 0x01 -> "TEXT";
            case 0x02 -> "IMAGE";
            case 0x03 -> "VOICE";
            case 0x04 -> "VIDEO";
            case 0x05 -> "FILE_INFO";
            case 0x06 -> "FILE_CHUNK";
            case 0x80 -> "HEARTBEAT";
            case 0x81 -> "AUTH";
            case 0x82 -> "LOGOUT";
            case 0x83 -> "SYNC";
            default -> "UNKNOWN";
        };
    }

    private String decodeFlags(byte flags) {
        StringBuilder sb = new StringBuilder();
        if ((flags & 0x01) != 0) sb.append("Compressed,");
        if ((flags & 0x02) != 0) sb.append("Encrypted,");
        if ((flags & 0x04) != 0) sb.append("Priority,");
        if ((flags & 0x08) != 0) sb.append("MultiDevice,");
        if (sb.length() == 0) return "None";
        return sb.substring(0, sb.length() - 1);
    }
}
