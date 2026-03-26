package com.ztrust.zchat.im.zsp.server;

import io.micrometer.core.instrument.Counter;
import io.micrometer.core.instrument.Gauge;
import io.micrometer.core.instrument.MeterRegistry;
import org.springframework.stereotype.Component;

import java.util.concurrent.atomic.AtomicLong;

/**
 * ZSP 网关指标：仅计数与 Gauge，不含用户标识或载荷；与「不导出诊断日志」配合用于运维观测。
 */
@Component
public final class ZspMetrics {

    private final MeterRegistry registry;
    private final AtomicLong tcpChannels = new AtomicLong();
    private final Counter offlineEnqueued;
    private final Counter forwardDelivered;
    private final Counter logout;

    public ZspMetrics(MeterRegistry registry) {
        this.registry = registry;
        Gauge.builder("zsp.gateway.tcp.channels", tcpChannels, AtomicLong::get).register(registry);
        this.offlineEnqueued = Counter.builder("zsp.gateway.offline.enqueued").register(registry);
        this.forwardDelivered = Counter.builder("zsp.gateway.forward.delivered").register(registry);
        this.logout = Counter.builder("zsp.gateway.session.logout").register(registry);
    }

    /** 策略关闭连接（与诊断码一致，便于对照）。 */
    public void recordClose(String reason) {
        registry.counter("zsp.gateway.closes", "reason", reason).increment();
    }

    public void recordOfflineEnqueued() {
        offlineEnqueued.increment();
    }

    public void recordForwardDelivered() {
        forwardDelivered.increment();
    }

    public void recordLogout() {
        logout.increment();
    }

    public void tcpConnected() {
        tcpChannels.incrementAndGet();
    }

    public void tcpDisconnected() {
        tcpChannels.updateAndGet(v -> v > 0 ? v - 1 : 0);
    }
}
