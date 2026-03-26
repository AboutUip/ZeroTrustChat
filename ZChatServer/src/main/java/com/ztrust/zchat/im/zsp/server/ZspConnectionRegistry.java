package com.ztrust.zchat.im.zsp.server;

import io.netty.channel.Channel;
import org.springframework.stereotype.Component;

import java.util.concurrent.ConcurrentHashMap;

/**
 * 在线用户（16B userId）→ TCP 连接，用于服务端转发 ZSP 帧。
 */
@Component
public final class ZspConnectionRegistry {

    private final ConcurrentHashMap<ZspByteArrayKey, Channel> userIdToChannel = new ConcurrentHashMap<>();

    public void register(byte[] userId16, Channel ch) {
        if (userId16 == null) {
            return;
        }
        userIdToChannel.put(new ZspByteArrayKey(userId16), ch);
    }

    public void unregister(byte[] userId16) {
        if (userId16 == null) {
            return;
        }
        userIdToChannel.remove(new ZspByteArrayKey(userId16));
    }

    public Channel findChannel(byte[] userId16) {
        if (userId16 == null) {
            return null;
        }
        return userIdToChannel.get(new ZspByteArrayKey(userId16));
    }

    public void replace(byte[] userId16, Channel ch) {
        register(userId16, ch);
    }
}
