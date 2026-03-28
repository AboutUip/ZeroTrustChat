package com.kite.zchat.core;

import java.util.Objects;

public final class ServerEndpoint {
    private final String host;
    private final int port;

    public ServerEndpoint(String host, int port) {
        this.host = host;
        this.port = port;
    }

    public String host() {
        return host;
    }

    public int port() {
        return port;
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) {
            return true;
        }
        if (!(o instanceof ServerEndpoint)) {
            return false;
        }
        ServerEndpoint that = (ServerEndpoint) o;
        return port == that.port && Objects.equals(host, that.host);
    }

    @Override
    public int hashCode() {
        return Objects.hash(host, port);
    }
}
