package com.kite.zchat.zsp;

import com.kite.zchat.core.ServerEndpoint;

import java.io.IOException;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;

/** 通过 ZSP 调用本地口令注册 / 登录。 */
public final class ZspLocalAuthClient {

    private static final int CONNECT_TIMEOUT_MS = 12_000;
    private static final int READ_TIMEOUT_MS = 20_000;

    private ZspLocalAuthClient() {}

    public static boolean registerLocalUser(byte[] userId16, String passwordUtf8, String recoveryUtf8, ServerEndpoint endpoint)
            throws IOException {
        byte[] pw = passwordUtf8.getBytes(StandardCharsets.UTF_8);
        byte[] rec = recoveryUtf8.getBytes(StandardCharsets.UTF_8);
        byte[] payload = buildRegisterPayload(userId16, pw, rec);
        byte[] request = ZspFrameCodec.encodeFrame(ZspProtocolConstants.LOCAL_REGISTER, 0, 0L, 1L, payload);
        try (Socket socket = openSocket(endpoint)) {
            OutputStream out = socket.getOutputStream();
            out.write(request);
            out.flush();
            ZspFrameCodec.ParsedFrame parsed = ZspFrameCodec.readFrame(socket.getInputStream());
            if (parsed.messageType != ZspProtocolConstants.LOCAL_REGISTER) {
                return false;
            }
            if (parsed.payload == null || parsed.payload.length != 1) {
                return false;
            }
            return parsed.payload[0] == 1;
        }
    }

    public static byte[] authWithLocalPassword(byte[] userId16, String passwordUtf8, ServerEndpoint endpoint)
            throws IOException {
        byte[] pw = passwordUtf8.getBytes(StandardCharsets.UTF_8);
        byte[] payload = buildPasswordAuthPayload(userId16, pw);
        byte[] request = ZspFrameCodec.encodeFrame(ZspProtocolConstants.LOCAL_PASSWORD_AUTH, 0, 0L, 1L, payload);
        try (Socket socket = openSocket(endpoint)) {
            OutputStream out = socket.getOutputStream();
            out.write(request);
            out.flush();
            ZspFrameCodec.ParsedFrame parsed = ZspFrameCodec.readFrame(socket.getInputStream());
            if (parsed.messageType != ZspProtocolConstants.LOCAL_PASSWORD_AUTH) {
                return null;
            }
            if (parsed.payload == null || parsed.payload.length == 0) {
                return null;
            }
            return parsed.payload;
        }
    }

    private static Socket openSocket(ServerEndpoint endpoint) throws IOException {
        Socket socket = new Socket();
        socket.connect(new InetSocketAddress(endpoint.host(), endpoint.port()), CONNECT_TIMEOUT_MS);
        socket.setSoTimeout(READ_TIMEOUT_MS);
        return socket;
    }

    private static byte[] buildRegisterPayload(byte[] userId16, byte[] passwordUtf8, byte[] recoveryUtf8) {
        if (userId16 == null || userId16.length != ZspProtocolConstants.USER_ID_SIZE) {
            throw new IllegalArgumentException("userId");
        }
        ByteBuffer buf = ByteBuffer.allocate(ZspProtocolConstants.USER_ID_SIZE + 2 + passwordUtf8.length + 2 + recoveryUtf8.length)
                .order(ByteOrder.BIG_ENDIAN);
        buf.put(userId16);
        buf.putShort((short) passwordUtf8.length);
        buf.put(passwordUtf8);
        buf.putShort((short) recoveryUtf8.length);
        buf.put(recoveryUtf8);
        return buf.array();
    }

    /** 供 {@link com.kite.zchat.zsp.ZspSessionManager} 与单帧登录共用。 */
    public static byte[] buildPasswordAuthPayload(byte[] userId16, byte[] passwordUtf8) {
        if (userId16 == null || userId16.length != ZspProtocolConstants.USER_ID_SIZE) {
            throw new IllegalArgumentException("userId");
        }
        ByteBuffer buf = ByteBuffer.allocate(ZspProtocolConstants.USER_ID_SIZE + 2 + passwordUtf8.length)
                .order(ByteOrder.BIG_ENDIAN);
        buf.put(userId16);
        buf.putShort((short) passwordUtf8.length);
        buf.put(passwordUtf8);
        return buf.array();
    }
}
