package com.kite.zchat.zsp;

import java.io.IOException;
import java.io.InputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.security.SecureRandom;
import java.util.Arrays;

public final class ZspFrameCodec {

    private static final SecureRandom RANDOM = new SecureRandom();

    private ZspFrameCodec() {}

    public static byte[] encodeFrame(int messageType, int flags, long sessionId, long sequence, byte[] payload) {
        if (payload == null) {
            throw new IllegalArgumentException("payload");
        }
        byte[] meta = buildMinimalMeta();
        ByteBuffer hb = ByteBuffer.allocate(ZspProtocolConstants.HEADER_LENGTH).order(ByteOrder.BIG_ENDIAN);
        hb.putShort((short) ZspProtocolConstants.MAGIC);
        hb.put((byte) ZspProtocolConstants.PROTOCOL_VERSION);
        hb.put((byte) messageType);
        hb.put((byte) flags);
        hb.put((byte) 0);
        hb.putInt((int) (sessionId & 0xFFFFFFFFL));
        hb.putInt((int) (sequence & 0xFFFFFFFFL));
        hb.putShort((short) payload.length);
        byte[] header = hb.array();
        byte[] tag = new byte[ZspProtocolConstants.AUTH_TAG_LENGTH];
        return concat(header, meta, payload, tag);
    }

    public static final class ParsedFrame {
        public final int messageType;
        public final byte[] payload;

        public ParsedFrame(int messageType, byte[] payload) {
            this.messageType = messageType;
            this.payload = payload;
        }
    }

    public static ParsedFrame readFrame(InputStream in) throws IOException {
        byte[] header = readFully(in, ZspProtocolConstants.HEADER_LENGTH);
        int magic = ((header[0] & 0xFF) << 8) | (header[1] & 0xFF);
        if (magic != ZspProtocolConstants.MAGIC) {
            throw new IOException("Invalid ZSP magic");
        }
        int messageType = header[3] & 0xFF;
        int payloadLen = ((header[14] & 0xFF) << 8) | (header[15] & 0xFF);
        if (payloadLen < 0 || payloadLen > 65535) {
            throw new IOException("Invalid payload length");
        }
        byte[] metaPrefix = readFully(in, 2);
        int metaLen = ((metaPrefix[0] & 0xFF) << 8) | (metaPrefix[1] & 0xFF);
        if (metaLen < ZspProtocolConstants.MIN_META_LENGTH || metaLen > 4096) {
            throw new IOException("Invalid meta length");
        }
        readFully(in, metaLen - 2);
        byte[] payload = payloadLen > 0 ? readFully(in, payloadLen) : new byte[0];
        readFully(in, ZspProtocolConstants.AUTH_TAG_LENGTH);
        return new ParsedFrame(messageType, payload);
    }

    private static byte[] buildMinimalMeta() {
        byte[] meta = new byte[ZspProtocolConstants.MIN_META_LENGTH];
        ByteBuffer buf = ByteBuffer.wrap(meta).order(ByteOrder.BIG_ENDIAN);
        buf.putShort((short) ZspProtocolConstants.MIN_META_LENGTH);
        buf.putLong(System.currentTimeMillis());
        byte[] nonce = new byte[12];
        RANDOM.nextBytes(nonce);
        buf.put(nonce);
        buf.putInt(0);
        return meta;
    }

    private static byte[] readFully(InputStream in, int len) throws IOException {
        byte[] out = new byte[len];
        int off = 0;
        while (off < len) {
            int n = in.read(out, off, len - off);
            if (n < 0) {
                throw new IOException("Unexpected end of stream");
            }
            off += n;
        }
        return out;
    }

    private static byte[] concat(byte[] a, byte[] b, byte[] c, byte[] d) {
        byte[] r = Arrays.copyOf(a, a.length + b.length + c.length + d.length);
        System.arraycopy(b, 0, r, a.length, b.length);
        System.arraycopy(c, 0, r, a.length + b.length, c.length);
        System.arraycopy(d, 0, r, a.length + b.length + c.length, d.length);
        return r;
    }
}
