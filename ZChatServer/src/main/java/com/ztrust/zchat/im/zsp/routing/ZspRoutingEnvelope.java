package com.ztrust.zchat.im.zsp.routing;

import com.ztrust.zchat.im.zsp.ZspConstants;

import java.util.Arrays;
import java.util.Optional;

/**
 * 网关路由约定：在 Java 不解密 E2E 密文时，仍需要 16B imSessionId 与可选 16B 对端 userId。
 * 明文布局：{@code imSessionId(16) || toUserId(16) || ciphertext...}；若仅单聊存储可省略 toUserId（全 0 表示不转发）。
 */
public final class ZspRoutingEnvelope {

    private ZspRoutingEnvelope() {}

    public static Optional<Split> splitOpaque(byte[] payload, int minPrefixBytes) {
        if (payload == null || payload.length < minPrefixBytes) {
            return Optional.empty();
        }
        byte[] imSessionId = Arrays.copyOfRange(payload, 0, ZspConstants.USER_ID_SIZE);
        if (minPrefixBytes == ZspConstants.USER_ID_SIZE) {
            return Optional.of(new Split(imSessionId, null, Arrays.copyOfRange(payload, ZspConstants.USER_ID_SIZE, payload.length)));
        }
        if (minPrefixBytes >= ZspConstants.USER_ID_SIZE * 2) {
            byte[] toUserId = Arrays.copyOfRange(payload, ZspConstants.USER_ID_SIZE, ZspConstants.USER_ID_SIZE * 2);
            byte[] rest = Arrays.copyOfRange(payload, ZspConstants.USER_ID_SIZE * 2, payload.length);
            return Optional.of(new Split(imSessionId, toUserId, rest));
        }
        return Optional.empty();
    }

    public record Split(byte[] imSessionId, byte[] toUserIdOrNull, byte[] body) {}
}
