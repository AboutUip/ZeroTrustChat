package com.ztrust.zchat.im.zsp;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.Optional;

public record ZspAuthPayload(byte[] userId, byte[] token) {

    public static Optional<ZspAuthPayload> parse(byte[] payload) {
        if (payload == null || payload.length < 4) {
            return Optional.empty();
        }
        ByteBuffer buf = ByteBuffer.wrap(payload).order(ByteOrder.BIG_ENDIAN);
        int userLen = buf.getShort() & 0xFFFF;
        if (payload.length < 2 + userLen + 2) {
            return Optional.empty();
        }
        byte[] userId = new byte[userLen];
        buf.get(userId);
        int tokenLen = buf.getShort() & 0xFFFF;
        if (tokenLen > ZspConstants.MAX_TOKEN_LENGTH || buf.remaining() < tokenLen) {
            return Optional.empty();
        }
        byte[] token = new byte[tokenLen];
        buf.get(token);
        if (buf.hasRemaining()) {
            return Optional.empty();
        }
        return Optional.of(new ZspAuthPayload(userId, token));
    }
}
