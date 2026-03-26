package com.ztrust.zchat.im.zsp.server;

import com.ztrust.zchat.im.zsp.security.ZspFrameTagMode;
import org.springframework.boot.context.properties.ConfigurationProperties;

@ConfigurationProperties(prefix = "zchat.zsp")
public class ZspServerProperties {

    private boolean enabled = true;

    private int port = 8848;

    private int bossThreads = 1;

    private int workerThreads = 0;

    private int readerIdleSeconds = 90;

    /** 心跳回显（对齐 docs/03-Business/04-Session.md 保活语义）。 */
    private boolean heartbeatEcho = true;

    /** 启用序列号 + Meta.nonce 防重放（对齐 02-ZSP-Protocol.md 8.4，简化实现）。 */
    private boolean replayProtectionEnabled = true;

    /**
     * 要求 Header.Flags 含 Encrypted；关闭可与旧客户端互操作（开发期）。
     */
    private boolean requireEncryptedFlag = false;

    /**
     * 密文 opaque 路由前缀最小字节数：16（仅 imSessionId）或 32（imSessionId + toUserId）。
     */
    private int opaqueRoutingMinBytes = 32;

    /**
     * Auth Tag 策略：NONE=全零；HMAC_SHA256_128=HMAC 截断 16B（非协议 AES-GCM，须与客户端一致）。
     */
    private ZspFrameTagMode frameTagMode = ZspFrameTagMode.NONE;

    /**
     * frame-tag-mode=HMAC_SHA256_128 时必填；建议 ≥32 字符或环境变量注入。
     */
    private String frameIntegritySecret = "";

    /** 入站是否校验 Auth Tag（HMAC 模式下建议开启）。 */
    private boolean verifyInboundAuthTag = false;

    /** Meta.timestamp 与服务器时间允许偏差（分钟），对齐 8.4 Timestamp 窗口；≤0 关闭。 */
    private int replayTimestampWindowMinutes = 5;

    /** Flags.Compressed=1 时是否直接断开（未实现解压）。生产建议 true。 */
    private boolean rejectCompressedPayload = true;

    /** 转发失败时是否写入内存离线队列。 */
    private boolean offlineQueueEnabled = true;

    private int offlineQueueMaxPerUser = 256;

    /**
     * 是否输出 ZSP 路径上的诊断日志（帧拒绝原因、管道异常等）。生产默认 false，避免日志外泄或被集中采集时带入敏感上下文；
     * 排障时可临时开启。见 docs/03-Business/01-SpringBoot.md 第八节。
     */
    private boolean diagnosticLogging = false;

    private final NativePaths nativePaths = new NativePaths();

    public boolean isEnabled() {
        return enabled;
    }

    public void setEnabled(boolean enabled) {
        this.enabled = enabled;
    }

    public int getPort() {
        return port;
    }

    public void setPort(int port) {
        this.port = port;
    }

    public int getBossThreads() {
        return bossThreads;
    }

    public void setBossThreads(int bossThreads) {
        this.bossThreads = bossThreads;
    }

    public int getWorkerThreads() {
        return workerThreads;
    }

    public void setWorkerThreads(int workerThreads) {
        this.workerThreads = workerThreads;
    }

    public int getReaderIdleSeconds() {
        return readerIdleSeconds;
    }

    public void setReaderIdleSeconds(int readerIdleSeconds) {
        this.readerIdleSeconds = readerIdleSeconds;
    }

    public boolean isHeartbeatEcho() {
        return heartbeatEcho;
    }

    public void setHeartbeatEcho(boolean heartbeatEcho) {
        this.heartbeatEcho = heartbeatEcho;
    }

    public boolean isReplayProtectionEnabled() {
        return replayProtectionEnabled;
    }

    public void setReplayProtectionEnabled(boolean replayProtectionEnabled) {
        this.replayProtectionEnabled = replayProtectionEnabled;
    }

    public boolean isRequireEncryptedFlag() {
        return requireEncryptedFlag;
    }

    public void setRequireEncryptedFlag(boolean requireEncryptedFlag) {
        this.requireEncryptedFlag = requireEncryptedFlag;
    }

    public int getOpaqueRoutingMinBytes() {
        return opaqueRoutingMinBytes;
    }

    public void setOpaqueRoutingMinBytes(int opaqueRoutingMinBytes) {
        this.opaqueRoutingMinBytes = opaqueRoutingMinBytes;
    }

    public ZspFrameTagMode getFrameTagMode() {
        return frameTagMode;
    }

    public void setFrameTagMode(ZspFrameTagMode frameTagMode) {
        this.frameTagMode = frameTagMode;
    }

    public String getFrameIntegritySecret() {
        return frameIntegritySecret;
    }

    public void setFrameIntegritySecret(String frameIntegritySecret) {
        this.frameIntegritySecret = frameIntegritySecret;
    }

    public boolean isVerifyInboundAuthTag() {
        return verifyInboundAuthTag;
    }

    public void setVerifyInboundAuthTag(boolean verifyInboundAuthTag) {
        this.verifyInboundAuthTag = verifyInboundAuthTag;
    }

    public int getReplayTimestampWindowMinutes() {
        return replayTimestampWindowMinutes;
    }

    public void setReplayTimestampWindowMinutes(int replayTimestampWindowMinutes) {
        this.replayTimestampWindowMinutes = replayTimestampWindowMinutes;
    }

    public boolean isRejectCompressedPayload() {
        return rejectCompressedPayload;
    }

    public void setRejectCompressedPayload(boolean rejectCompressedPayload) {
        this.rejectCompressedPayload = rejectCompressedPayload;
    }

    public boolean isOfflineQueueEnabled() {
        return offlineQueueEnabled;
    }

    public void setOfflineQueueEnabled(boolean offlineQueueEnabled) {
        this.offlineQueueEnabled = offlineQueueEnabled;
    }

    public int getOfflineQueueMaxPerUser() {
        return offlineQueueMaxPerUser;
    }

    public void setOfflineQueueMaxPerUser(int offlineQueueMaxPerUser) {
        this.offlineQueueMaxPerUser = offlineQueueMaxPerUser;
    }

    public boolean isDiagnosticLogging() {
        return diagnosticLogging;
    }

    public void setDiagnosticLogging(boolean diagnosticLogging) {
        this.diagnosticLogging = diagnosticLogging;
    }

    public NativePaths getNative() {
        return nativePaths;
    }

    public static final class NativePaths {
        private String dataDir;
        private String indexDir;
        /**
         * 若 {@link com.ztrust.zchat.im.jni.ZChatIMNative#initialize(String, String)} 返回 false 而 MM2 需要口令，
         * 与 {@link com.ztrust.zchat.im.jni.ZChatIMNative#initializeWithPassphrase(String, String, String)} 对齐。
         */
        private String passphrase = "";

        public String getDataDir() {
            return dataDir;
        }

        public void setDataDir(String dataDir) {
            this.dataDir = dataDir;
        }

        public String getIndexDir() {
            return indexDir;
        }

        public void setIndexDir(String indexDir) {
            this.indexDir = indexDir;
        }

        public String getPassphrase() {
            return passphrase;
        }

        public void setPassphrase(String passphrase) {
            this.passphrase = passphrase;
        }
    }
}
