package com.ztrust.zchat.im.zsp.server;

import org.springframework.boot.context.properties.EnableConfigurationProperties;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;

@Configuration
@EnableConfigurationProperties(ZspServerProperties.class)
public class ZspServerConfig {

    @Bean
    public ZspJniBridge zspJniBridge(ZspServerProperties properties) {
        if (properties.isJniEnabled()) {
            return new ZChatIMNativeJniBridge(properties);
        }
        return new NoopZspJniBridge();
    }
}
