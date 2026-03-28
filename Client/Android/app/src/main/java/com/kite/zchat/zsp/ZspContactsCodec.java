package com.kite.zchat.zsp;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;

/** 解析通讯录相关 ZSP 应答（与 ZChatServer 网关编码一致）。 */
public final class ZspContactsCodec {

    public static final class GroupInfo {
        public final String nameUtf8;
        public final int memberCount;

        public GroupInfo(String nameUtf8, int memberCount) {
            this.nameUtf8 = nameUtf8 != null ? nameUtf8 : "";
            this.memberCount = memberCount;
        }
    }

    private ZspContactsCodec() {}

    /** 好友列表 / 群列表：count(u32 BE) ‖ count×16B id。 */
    public static byte[][] parseIdList16(byte[] payload) {
        if (payload == null || payload.length < 4) {
            return new byte[0][];
        }
        ByteBuffer buf = ByteBuffer.wrap(payload).order(ByteOrder.BIG_ENDIAN);
        int n = buf.getInt();
        if (n < 0 || n > 100_000) {
            return new byte[0][];
        }
        int need = 4 + n * ZspProtocolConstants.USER_ID_SIZE;
        if (payload.length < need) {
            return new byte[0][];
        }
        byte[][] out = new byte[n][];
        for (int i = 0; i < n; i++) {
            byte[] id = new byte[ZspProtocolConstants.USER_ID_SIZE];
            buf.get(id);
            out[i] = id;
        }
        return out;
    }

    /** 群信息：nameLen(u16 BE) ‖ name UTF-8 ‖ memberCount(u32 BE) ‖ memberCount×userId(16)。 */
    public static GroupInfo parseGroupInfo(byte[] payload) {
        if (payload == null || payload.length < 2) {
            return new GroupInfo("", 0);
        }
        ByteBuffer buf = ByteBuffer.wrap(payload).order(ByteOrder.BIG_ENDIAN);
        int nameLen = buf.getShort() & 0xFFFF;
        if (payload.length < 2 + nameLen + 4) {
            return new GroupInfo("", 0);
        }
        byte[] nameRaw = new byte[nameLen];
        buf.get(nameRaw);
        String name = new String(nameRaw, StandardCharsets.UTF_8);
        int memberCount = buf.getInt();
        if (memberCount < 0) {
            memberCount = 0;
        }
        int expectedRemain = memberCount * ZspProtocolConstants.USER_ID_SIZE;
        int remaining = payload.length - buf.position();
        if (remaining < expectedRemain) {
            memberCount = remaining / ZspProtocolConstants.USER_ID_SIZE;
        }
        return new GroupInfo(name, memberCount);
    }
}
