package com.kite.zchat.zsp;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

/** 明文 IM：TEXT 载荷与 SYNC 应答解析（与网关 cleartext 约定一致）。 */
public final class ZspChatWire {

    public static final class TextSendResult {
        public final boolean ok;
        /** 成功时 16 字节 messageId；失败为空。 */
        public final byte[] messageId16;

        public TextSendResult(boolean ok, byte[] messageId16) {
            this.ok = ok;
            this.messageId16 = messageId16;
        }
    }

    public static final class SyncRow {
        public final byte[] messageId16;
        /** 解密后的明文载荷：toUserId(16) ‖ UTF-8 文本（与 StoreMessage 一致）。 */
        public final byte[] plainPayload;

        public SyncRow(byte[] messageId16, byte[] plainPayload) {
            this.messageId16 = messageId16;
            this.plainPayload = plainPayload;
        }
    }

    private ZspChatWire() {}

    /** imSession(16) ‖ toUserId(16) ‖ textUtf8 */
    public static byte[] buildTextPayload(byte[] imSession16, byte[] toUserId16, String textUtf8) {
        if (imSession16 == null
                || imSession16.length != ZspProtocolConstants.USER_ID_SIZE
                || toUserId16 == null
                || toUserId16.length != ZspProtocolConstants.USER_ID_SIZE) {
            return new byte[0];
        }
        byte[] t = textUtf8 != null ? textUtf8.getBytes(StandardCharsets.UTF_8) : new byte[0];
        if (t.length > 60000) {
            return new byte[0];
        }
        byte[] out = new byte[32 + t.length];
        System.arraycopy(imSession16, 0, out, 0, 16);
        System.arraycopy(toUserId16, 0, out, 16, 16);
        System.arraycopy(t, 0, out, 32, t.length);
        return out;
    }

    /** 首次：仅 imSession(16)；增量：im(16)‖listUserId(16)‖lastMsgId(16)‖limit(u32) */
    public static byte[] buildSyncPayloadInitial(byte[] imSession16) {
        if (imSession16 == null || imSession16.length != ZspProtocolConstants.USER_ID_SIZE) {
            return new byte[0];
        }
        return imSession16.clone();
    }

    public static byte[] buildSyncPayloadSince(
            byte[] imSession16, byte[] listUserId16, byte[] lastMsgId16, int limit) {
        if (imSession16 == null
                || imSession16.length != ZspProtocolConstants.USER_ID_SIZE
                || listUserId16 == null
                || listUserId16.length != ZspProtocolConstants.USER_ID_SIZE
                || lastMsgId16 == null
                || lastMsgId16.length != 16) {
            return new byte[0];
        }
        ByteBuffer buf = ByteBuffer.allocate(52).order(ByteOrder.BIG_ENDIAN);
        buf.put(imSession16);
        buf.put(listUserId16);
        buf.put(lastMsgId16);
        buf.putInt(limit);
        return buf.array();
    }

    public static List<SyncRow> parseSyncResponse(byte[] payload) {
        List<SyncRow> out = new ArrayList<>();
        if (payload == null || payload.length < 4) {
            return out;
        }
        ByteBuffer buf = ByteBuffer.wrap(payload).order(ByteOrder.BIG_ENDIAN);
        int count = buf.getInt();
        if (count < 0 || count > 10000) {
            return out;
        }
        for (int i = 0; i < count && buf.remaining() >= 4; i++) {
            int rowLen = buf.getInt();
            if (rowLen < 0 || rowLen > buf.remaining()) {
                break;
            }
            byte[] row = new byte[rowLen];
            buf.get(row);
            if (row.length < 16 + 4) {
                continue;
            }
            ByteBuffer rb = ByteBuffer.wrap(row).order(ByteOrder.BIG_ENDIAN);
            byte[] msgId = new byte[16];
            rb.get(msgId);
            int plen = rb.getInt();
            if (plen < 0 || plen > rb.remaining()) {
                continue;
            }
            byte[] plain = new byte[plen];
            rb.get(plain);
            out.add(new SyncRow(msgId, plain));
        }
        return out;
    }

    /** plainPayload: toUserId(16) ‖ utf8（与 cleartextImStore 写入 MM2 的 body 一致） */
    public static byte[] extractToUser(byte[] plainPayload) {
        if (plainPayload == null || plainPayload.length < 16) {
            return new byte[0];
        }
        byte[] to = new byte[16];
        System.arraycopy(plainPayload, 0, to, 0, 16);
        return to;
    }

    public static String extractTextUtf8(byte[] plainPayload) {
        if (plainPayload == null || plainPayload.length <= 16) {
            return "";
        }
        return new String(plainPayload, 16, plainPayload.length - 16, StandardCharsets.UTF_8);
    }
}
