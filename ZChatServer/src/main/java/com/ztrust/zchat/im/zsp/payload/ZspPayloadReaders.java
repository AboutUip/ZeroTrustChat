package com.ztrust.zchat.im.zsp.payload;

import com.ztrust.zchat.im.zsp.ZspConstants;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.HexFormat;
import java.util.Optional;

/**
 * ZSP 第六节载荷解析（明文模式）。密文模式下由 {@link com.ztrust.zchat.im.zsp.routing.ZspRoutingEnvelope} 处理路由前缀。
 */
public final class ZspPayloadReaders {

    private ZspPayloadReaders() {}

    public static String fileIdBytesToString(byte[] uuid16) {
        if (uuid16 == null || uuid16.length != 16) {
            return "";
        }
        return HexFormat.of().formatHex(uuid16);
    }

    public static Optional<FileChunk> parseFileChunk(byte[] p) {
        if (p == null || p.length < 16 + 4) {
            return Optional.empty();
        }
        ByteBuffer buf = ByteBuffer.wrap(p).order(ByteOrder.BIG_ENDIAN);
        byte[] fid = new byte[16];
        buf.get(fid);
        int idx = buf.getInt();
        byte[] data = new byte[buf.remaining()];
        buf.get(data);
        return Optional.of(new FileChunk(fid, idx, data));
    }

    public static Optional<FileComplete> parseFileComplete(byte[] p) {
        if (p == null || p.length < 16 + 32 + 1) {
            return Optional.empty();
        }
        ByteBuffer buf = ByteBuffer.wrap(p).order(ByteOrder.BIG_ENDIAN);
        byte[] fid = new byte[16];
        buf.get(fid);
        byte[] sha = new byte[32];
        buf.get(sha);
        int st = buf.get() & 0xFF;
        return Optional.of(new FileComplete(fid, sha, st));
    }

    public static Optional<CallSignal> parseCallSignal(byte[] p) {
        if (p == null || p.length < 1 + 4) {
            return Optional.empty();
        }
        ByteBuffer buf = ByteBuffer.wrap(p).order(ByteOrder.BIG_ENDIAN);
        int sub = buf.get() & 0xFF;
        int dur = buf.getInt();
        byte[] data = new byte[buf.remaining()];
        buf.get(data);
        return Optional.of(new CallSignal(sub, dur, data));
    }

    /** 前 16 字节为 callId（MESSAGE_ID_SIZE），与协议 6.6 建议一致。 */
    public static Optional<byte[]> extractCallIdFromSignalData(byte[] data) {
        if (data == null || data.length < ZspConstants.USER_ID_SIZE) {
            return Optional.empty();
        }
        return Optional.of(Arrays.copyOf(data, ZspConstants.USER_ID_SIZE));
    }

    public static Optional<GroupPair> parseGroupPair(byte[] p) {
        if (p == null || p.length < 32) {
            return Optional.empty();
        }
        byte[] a = Arrays.copyOfRange(p, 0, 16);
        byte[] b = Arrays.copyOfRange(p, 16, 32);
        byte[] rest = p.length > 32 ? Arrays.copyOfRange(p, 32, p.length) : new byte[0];
        return Optional.of(new GroupPair(a, b, rest));
    }

    public static Optional<String> parseUtf8Prefixed(byte[] p) {
        if (p == null || p.length < 2) {
            return Optional.empty();
        }
        ByteBuffer buf = ByteBuffer.wrap(p).order(ByteOrder.BIG_ENDIAN);
        int n = buf.getShort() & 0xFFFF;
        if (n > 2048 || buf.remaining() < n) {
            return Optional.empty();
        }
        byte[] raw = new byte[n];
        buf.get(raw);
        return Optional.of(new String(raw, StandardCharsets.UTF_8));
    }

    public static Optional<MessageIdTs> parseMessageIdAndOptionalTs(byte[] p) {
        if (p == null || p.length < 16) {
            return Optional.empty();
        }
        byte[] mid = Arrays.copyOfRange(p, 0, 16);
        long ts = 0L;
        if (p.length >= 24) {
            ts = ByteBuffer.wrap(p, 16, 8).order(ByteOrder.BIG_ENDIAN).getLong();
        }
        return Optional.of(new MessageIdTs(mid, ts));
    }

    public static Optional<PeerCall> parsePeerAndCallKind(byte[] p) {
        if (p == null || p.length < 16 + 4) {
            return Optional.empty();
        }
        ByteBuffer buf = ByteBuffer.wrap(p).order(ByteOrder.BIG_ENDIAN);
        byte[] peer = new byte[16];
        buf.get(peer);
        int kind = buf.getInt();
        return Optional.of(new PeerCall(peer, kind));
    }

    public record FileChunk(byte[] fileId16, int chunkIndex, byte[] chunkData) {}

    public record FileComplete(byte[] fileId16, byte[] sha256, int status) {}

    public record CallSignal(int subType, int durationMs, byte[] data) {}

    public record GroupPair(byte[] idA16, byte[] idB16, byte[] rest) {}

    public record MessageIdTs(byte[] messageId16, long timestampMs) {}

    public record PeerCall(byte[] peerUserId16, int callKind) {}
}
