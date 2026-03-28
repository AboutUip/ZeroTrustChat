package com.ztrust.zchat.im.zsp.server;

import com.ztrust.zchat.im.zsp.ZspConstants;
import com.ztrust.zchat.im.zsp.ZspFrame;
import com.ztrust.zchat.im.zsp.ZspFrameWireEncoder;
import com.ztrust.zchat.im.zsp.ZspHeader;
import com.ztrust.zchat.im.zsp.ZspMeta;
import io.netty.channel.ChannelHandlerContext;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.stereotype.Component;

import java.util.ArrayDeque;
import java.util.Deque;
import java.util.concurrent.ConcurrentHashMap;

/**
 * 目标用户离线时暂存待转发的业务帧（内存队列；生产可换 Redis 等）。
 */
@Component
public final class ZspOfflineMessageQueue {

    private final ZspServerProperties props;
    private final ZspMetrics metrics;
    private final ConcurrentHashMap<ZspByteArrayKey, Deque<PendingRelay>> byUser = new ConcurrentHashMap<>();

    @Autowired
    public ZspOfflineMessageQueue(ZspServerProperties props, @Autowired(required = false) ZspMetrics metrics) {
        this.props = props;
        this.metrics = metrics;
    }

    public void enqueue(byte[] targetUserId16, long zspHeaderSessionId, int messageType, int flags, byte[] payload) {
        if (!props.isOfflineQueueEnabled() || targetUserId16 == null || targetUserId16.length != ZspConstants.USER_ID_SIZE) {
            return;
        }
        PendingRelay p = new PendingRelay(zspHeaderSessionId, messageType, flags, payload);
        ZspByteArrayKey key = new ZspByteArrayKey(targetUserId16);
        byUser.compute(
                key,
                (k, dq) -> {
                    Deque<PendingRelay> q = dq != null ? dq : new ArrayDeque<>();
                    while (q.size() >= props.getOfflineQueueMaxPerUser()) {
                        q.pollFirst();
                    }
                    q.addLast(p);
                    if (metrics != null) {
                        metrics.recordOfflineEnqueued();
                    }
                    return q;
                });
    }

    public void drainTo(ChannelHandlerContext recipientCtx, byte[] recipientUserId16, ZspOutboundFrameBuilder outbound) {
        if (!props.isOfflineQueueEnabled() || recipientUserId16 == null) {
            return;
        }
        ZspByteArrayKey key = new ZspByteArrayKey(recipientUserId16);
        Deque<PendingRelay> q = byUser.remove(key);
        if (q == null || q.isEmpty()) {
            return;
        }
        PendingRelay pending;
        while ((pending = q.pollFirst()) != null) {
            ZspFrame shell = syntheticRequest(pending);
            ZspFrame out =
                    outbound.replySameFlags(recipientCtx, shell, pending.messageType(), pending.payload());
            recipientCtx.writeAndFlush(ZspFrameWireEncoder.toByteBuf(recipientCtx.alloc(), out));
        }
    }

    private static ZspFrame syntheticRequest(PendingRelay p) {
        byte[] meta = ZspMeta.minimal(0L, new byte[12], 0);
        ZspHeader h =
                new ZspHeader(
                        ZspConstants.MAGIC,
                        ZspConstants.PROTOCOL_VERSION,
                        p.messageType(),
                        p.flags(),
                        0,
                        p.zspHeaderSessionId(),
                        0L,
                        0);
        return new ZspFrame(h, meta, new byte[0], new byte[ZspConstants.AUTH_TAG_LENGTH]);
    }

    public record PendingRelay(long zspHeaderSessionId, int messageType, int flags, byte[] payload) {}
}
