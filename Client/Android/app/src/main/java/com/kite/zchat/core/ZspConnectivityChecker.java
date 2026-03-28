package com.kite.zchat.core;

import androidx.annotation.Nullable;

import com.kite.zchat.zsp.ZspFrameCodec;
import com.kite.zchat.zsp.ZspProtocolConstants;

import java.io.IOException;
import java.net.InetSocketAddress;
import java.net.Socket;

/**
 * 先建立 TCP，再发送 ZSP HEARTBEAT 并校验回显，避免仅端口开放（如误连 HTTP）却当作 ZSP 可用。
 */
public final class ZspConnectivityChecker {

    /** 探测结果：不可达 / 已连上但非有效 ZSP 回显 / ZSP 心跳正常。 */
    public enum ProbeResult {
        UNREACHABLE,
        ZSP_HANDSHAKE_FAILED,
        ZSP_OK
    }

    /** {@link #result} 与可选的异常或解析说明（便于与服务端日志对照）。 */
    public static final class ZspProbeOutcome {
        public final ProbeResult result;
        @Nullable public final String detail;

        public ZspProbeOutcome(ProbeResult result, @Nullable String detail) {
            this.result = result;
            this.detail = detail;
        }
    }

    private final int connectTimeoutMs;
    private final int readTimeoutMs;

    public ZspConnectivityChecker(int connectTimeoutMs) {
        this(connectTimeoutMs, Math.max(8000, connectTimeoutMs * 2));
    }

    public ZspConnectivityChecker(int connectTimeoutMs, int readTimeoutMs) {
        this.connectTimeoutMs = connectTimeoutMs;
        this.readTimeoutMs = readTimeoutMs;
    }

    /**
     * 仅 TCP 连通性（不校验协议）。
     */
    public boolean canConnectTcpOnly(ServerEndpoint endpoint) {
        try (Socket socket = new Socket()) {
            socket.connect(new InetSocketAddress(endpoint.host(), endpoint.port()), connectTimeoutMs);
            return true;
        } catch (IOException ex) {
            return false;
        }
    }

    /**
     * 发送一帧 HEARTBEAT（空载荷），期望服务端回显 HEARTBEAT（默认 heartbeat-echo: true）。
     */
    public ZspProbeOutcome probeZsp(ServerEndpoint endpoint) {
        try (Socket socket = new Socket()) {
            socket.setTcpNoDelay(true);
            socket.connect(new InetSocketAddress(endpoint.host(), endpoint.port()), connectTimeoutMs);
            socket.setSoTimeout(readTimeoutMs);
            try {
                byte[] request =
                        ZspFrameCodec.encodeFrame(ZspProtocolConstants.HEARTBEAT, 0, 0L, 1L, new byte[0]);
                socket.getOutputStream().write(request);
                socket.getOutputStream().flush();
                ZspFrameCodec.ParsedFrame parsed = ZspFrameCodec.readFrame(socket.getInputStream());
                int got = parsed.messageType & 0xFF;
                int want = ZspProtocolConstants.HEARTBEAT & 0xFF;
                if (got == want) {
                    return new ZspProbeOutcome(ProbeResult.ZSP_OK, null);
                }
                return new ZspProbeOutcome(
                        ProbeResult.ZSP_HANDSHAKE_FAILED,
                        "unexpected messageType 0x" + Integer.toHexString(got) + " (expected 0x" + Integer.toHexString(want) + ")");
            } catch (IOException e) {
                return new ZspProbeOutcome(
                        ProbeResult.ZSP_HANDSHAKE_FAILED,
                        e.getClass().getSimpleName() + ": " + e.getMessage());
            }
        } catch (IOException e) {
            return new ZspProbeOutcome(
                    ProbeResult.UNREACHABLE,
                    e.getClass().getSimpleName() + ": " + e.getMessage());
        }
    }
}
