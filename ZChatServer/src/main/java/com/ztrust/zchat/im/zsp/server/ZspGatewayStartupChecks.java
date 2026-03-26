package com.ztrust.zchat.im.zsp.server;

import com.ztrust.zchat.im.zsp.security.ZspFrameTagMode;
import org.springframework.boot.autoconfigure.condition.ConditionalOnProperty;
import org.springframework.stereotype.Component;

/**
 * 启动期校验：避免 HMAC 帧标签已启用却无密钥时仍监听端口。
 */
@Component
@ConditionalOnProperty(prefix = "zchat.zsp", name = "enabled", havingValue = "true", matchIfMissing = true)
public final class ZspGatewayStartupChecks {

    public ZspGatewayStartupChecks(ZspServerProperties props) {
        if (props.getFrameTagMode() == ZspFrameTagMode.HMAC_SHA256_128) {
            String s = props.getFrameIntegritySecret();
            if (s == null || s.isBlank()) {
                throw new IllegalStateException(
                        "zchat.zsp.frame-integrity-secret is required when zchat.zsp.frame-tag-mode=HMAC_SHA256_128");
            }
        }
    }
}
