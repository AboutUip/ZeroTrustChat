package com.ztrust.zchat.im.zsp.server;

import java.util.logging.Level;
import java.util.logging.Logger;

/** 仅当 {@link ZspServerProperties#isDiagnosticLogging()} 为 true 时输出，避免默认向日志管道写入可关联业务的信息。 */
public final class ZspGatewayLog {

    private ZspGatewayLog() {}

    public static void diag(Logger log, ZspServerProperties props, Level level, String code) {
        if (props == null || !props.isDiagnosticLogging()) {
            return;
        }
        log.log(level, "[zsp] {0}", code);
    }
}
