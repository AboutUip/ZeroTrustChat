package com.ztrust.zchat.im.zsp.server;

import org.springframework.boot.actuate.health.Health;
import org.springframework.boot.actuate.health.HealthIndicator;
import org.springframework.stereotype.Component;

/**
 * ZSP TCP 监听是否与配置一致；不包含业务细节。
 */
@Component
public final class ZspHealthIndicator implements HealthIndicator {

    private final ZspServerProperties props;
    private final ZspNettyServer nettyServer;

    public ZspHealthIndicator(ZspServerProperties props, ZspNettyServer nettyServer) {
        this.props = props;
        this.nettyServer = nettyServer;
    }

    @Override
    public Health health() {
        if (!props.isEnabled()) {
            return Health.up().withDetail("zsp", "disabled").build();
        }
        if (nettyServer.isRunning()) {
            return Health.up().withDetail("zsp", "listening").withDetail("port", props.getPort()).build();
        }
        return Health.down().withDetail("zsp", "not_listening").build();
    }
}
