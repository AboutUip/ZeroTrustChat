package com.ztrust.zchat.boot;

import org.springframework.context.annotation.Configuration;
import org.springframework.web.servlet.config.annotation.ResourceHandlerRegistry;
import org.springframework.web.servlet.config.annotation.WebMvcConfigurer;

/**
 * 仅映射 {@code /ops/**} 到 {@code classpath:/static/ops/}，并依赖 {@code application.yml} 中
 * {@code spring.web.resources.add-mappings=false}，避免默认 {@code /**} 静态资源抢在 Actuator 之前匹配
 * {@code /actuator/prometheus} 等路径。
 */
@Configuration
public class OpsWebMvcConfiguration implements WebMvcConfigurer {

    @Override
    public void addResourceHandlers(ResourceHandlerRegistry registry) {
        registry.addResourceHandler("/ops/**").addResourceLocations("classpath:/static/ops/");
    }
}
