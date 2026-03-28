package com.kite.zchat.core;

import android.content.Context;
import android.content.SharedPreferences;

public final class ServerConfigStore {
    private static final String PREF_NAME = "zchat_server_config";
    private static final String KEY_HOST = "server_host";
    private static final String KEY_PORT = "server_port";
    /** 与 ZSP 端口独立：业务 HTTP 上注册 FCM 令牌，默认 8080。 */
    private static final String KEY_PUSH_HTTP_PORT = "push_http_port";
    /** {@code http} 或 {@code https}，默认 {@code http}（局域网调试）；生产建议 {@code https}。 */
    private static final String KEY_PUSH_SCHEME = "push_scheme";

    public static final int DEFAULT_PUSH_HTTP_PORT = 8080;

    private final SharedPreferences preferences;

    public ServerConfigStore(Context context) {
        this.preferences = context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE);
    }

    public ServerEndpoint getSavedEndpoint() {
        String host = preferences.getString(KEY_HOST, null);
        int port = preferences.getInt(KEY_PORT, -1);
        if (host == null || host.isBlank() || port < 1 || port > 65535) {
            return null;
        }
        return new ServerEndpoint(host.trim(), port);
    }

    public void saveEndpoint(ServerEndpoint endpoint) {
        preferences.edit()
                .putString(KEY_HOST, endpoint.host())
                .putInt(KEY_PORT, endpoint.port())
                .apply();
    }

    /** 业务 HTTP 端口（注册 FCM 等），默认 {@link #DEFAULT_PUSH_HTTP_PORT}。 */
    public int getPushHttpPort() {
        int p = preferences.getInt(KEY_PUSH_HTTP_PORT, DEFAULT_PUSH_HTTP_PORT);
        if (p < 1 || p > 65535) {
            return DEFAULT_PUSH_HTTP_PORT;
        }
        return p;
    }

    public void setPushHttpPort(int port) {
        if (port < 1 || port > 65535) {
            return;
        }
        preferences.edit().putInt(KEY_PUSH_HTTP_PORT, port).apply();
    }

    /** 返回 {@code http} 或 {@code https}。 */
    public String getPushScheme() {
        String s = preferences.getString(KEY_PUSH_SCHEME, "http");
        if (s == null) {
            return "http";
        }
        s = s.trim().toLowerCase();
        if ("https".equals(s)) {
            return "https";
        }
        return "http";
    }

    public void setPushScheme(String scheme) {
        if (scheme == null || scheme.isBlank()) {
            return;
        }
        String s = scheme.trim().toLowerCase();
        if ("http".equals(s) || "https".equals(s)) {
            preferences.edit().putString(KEY_PUSH_SCHEME, s).apply();
        }
    }

    /**
     * 注册 FCM 设备令牌的完整 URL（与 ZSP TCP 无关）。
     *
     * <p>约定：<code>POST {scheme}://{host}:{pushPort}/api/v1/devices/fcm-token</code>，JSON 见 {@link
     * com.kite.zchat.push.FcmTokenUploader}。
     */
    public String getPushRegisterUrl() {
        ServerEndpoint ep = getSavedEndpoint();
        if (ep == null) {
            return null;
        }
        String scheme = getPushScheme();
        int pushPort = getPushHttpPort();
        String host = ep.host().trim();
        if (host.contains(":") && !host.startsWith("[")) {
            host = "[" + host + "]";
        }
        return scheme + "://" + host + ":" + pushPort + "/api/v1/devices/fcm-token";
    }
}
