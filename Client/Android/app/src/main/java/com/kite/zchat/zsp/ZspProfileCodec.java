package com.kite.zchat.zsp;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;

/** 解析 {@link ZspProtocolConstants#USER_PROFILE_GET} 应答。 */
public final class ZspProfileCodec {

    public static final class UserProfile {
        public final String nicknameUtf8;
        public final byte[] avatarBytes;

        public UserProfile(String nicknameUtf8, byte[] avatarBytes) {
            this.nicknameUtf8 = nicknameUtf8 != null ? nicknameUtf8 : "";
            this.avatarBytes = avatarBytes;
        }
    }

    private ZspProfileCodec() {}

    public static UserProfile parseProfilePayload(byte[] payload) {
        if (payload == null || payload.length < 4) {
            return new UserProfile("", null);
        }
        ByteBuffer buf = ByteBuffer.wrap(payload).order(ByteOrder.BIG_ENDIAN);
        int nickLen = buf.getShort() & 0xFFFF;
        if (payload.length < 4 + nickLen) {
            return new UserProfile("", null);
        }
        byte[] nickRaw = new byte[nickLen];
        buf.get(nickRaw);
        int avLen = buf.getShort() & 0xFFFF;
        if (payload.length < 4 + nickLen + 2 + avLen) {
            return new UserProfile(new String(nickRaw, StandardCharsets.UTF_8), null);
        }
        byte[] av = new byte[avLen];
        buf.get(av);
        return new UserProfile(new String(nickRaw, StandardCharsets.UTF_8), av);
    }
}
