package com.ztrust.zchat.im.zsp.server;

import org.springframework.boot.context.properties.ConfigurationProperties;

@ConfigurationProperties(prefix = "zchat.zsp")
public class ZspServerProperties {

    private boolean enabled = true;

    private int port = 8848;

    private int bossThreads = 1;

    private int workerThreads = 0;

    private int readerIdleSeconds = 90;

    private boolean jniEnabled = false;

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

    public boolean isJniEnabled() {
        return jniEnabled;
    }

    public void setJniEnabled(boolean jniEnabled) {
        this.jniEnabled = jniEnabled;
    }

    public NativePaths getNative() {
        return nativePaths;
    }

    public static final class NativePaths {
        private String dataDir;
        private String indexDir;

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
    }
}
