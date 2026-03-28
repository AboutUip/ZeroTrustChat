package com.kite.zchat.ui;

import android.content.Context;

import com.kite.zchat.R;

import java.io.EOFException;
import java.io.IOException;
import java.net.ConnectException;
import java.net.SocketTimeoutException;
import java.net.UnknownHostException;

/**
 * 将 IOException 映射为可读说明（与权限无关；未声明 INTERNET 时系统会在安装时授予）。
 */
public final class NetworkErrorMessages {

    private NetworkErrorMessages() {}

    public static String fromIOException(Context context, IOException e) {
        Throwable t = e;
        while (t != null) {
            if (t instanceof SocketTimeoutException) {
                return context.getString(R.string.error_network_timeout);
            }
            if (t instanceof UnknownHostException) {
                return context.getString(R.string.error_network_unknown_host);
            }
            if (t instanceof ConnectException) {
                return context.getString(R.string.error_network_refused);
            }
            if (t instanceof EOFException) {
                return context.getString(R.string.error_network_server_closed);
            }
            String msg = t.getMessage();
            if (msg != null) {
                String m = msg.toLowerCase();
                if (m.contains("connection reset") || m.contains("broken pipe") || m.contains("connection aborted")) {
                    return context.getString(R.string.error_network_server_closed);
                }
                if (m.contains("timed out")) {
                    return context.getString(R.string.error_network_timeout);
                }
            }
            t = t.getCause();
        }
        String detail = e.getMessage();
        if (detail != null && !detail.isBlank()) {
            return context.getString(R.string.error_network_with_detail, detail);
        }
        return context.getString(R.string.error_network);
    }
}
