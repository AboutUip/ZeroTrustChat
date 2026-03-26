package com.ztrust.zchat.im.zsp.server;

import org.springframework.boot.context.properties.EnableConfigurationProperties;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;

@Configuration
@EnableConfigurationProperties(ZspServerProperties.class)
public class ZspServerConfig {

    /**
     * 必选：构造时加载并初始化 {@link com.ztrust.zchat.im.jni.ZChatIMNative}。须配置 {@code zchat.zsp.native.data-dir} /
     * {@code index-dir}。
     */
    @Bean
    public ZChatNativeOperations zChatNativeOperations(ZspServerProperties properties) {
        return new ZChatNativeOperations(properties);
    }
}
