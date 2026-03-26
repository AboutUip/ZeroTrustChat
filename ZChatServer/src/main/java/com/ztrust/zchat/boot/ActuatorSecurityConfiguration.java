package com.ztrust.zchat.boot;

import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.springframework.core.annotation.Order;
import org.springframework.security.config.Customizer;
import org.springframework.security.config.annotation.web.builders.HttpSecurity;
import org.springframework.security.config.annotation.web.configuration.EnableWebSecurity;
import org.springframework.security.config.annotation.web.configurers.AbstractHttpConfigurer;
import org.springframework.security.web.SecurityFilterChain;

/**
 * Actuator（健康/指标/Prometheus）与内嵌运维面板 {@code /ops/**} 仅用于观测，与 ZSP IM 链路无关。
 * <p>
 * - {@code /actuator/**}：HTTP Basic（便于 Prometheus/curl 等自动化）
 * - {@code /ops/**}：表单登录（浏览器友好）
 * - 其余 HTTP 路径：默认拒绝
 */
@Configuration
@EnableWebSecurity
public class ActuatorSecurityConfiguration {

    @Bean
    @Order(1)
    public SecurityFilterChain actuatorHttpBasic(HttpSecurity http) throws Exception {
        http.securityMatcher("/actuator/**")
                .authorizeHttpRequests(auth -> auth.anyRequest().authenticated())
                .httpBasic(Customizer.withDefaults())
                .csrf(AbstractHttpConfigurer::disable);
        return http.build();
    }

    @Bean
    @Order(2)
    public SecurityFilterChain opsFormLogin(HttpSecurity http) throws Exception {
        http.securityMatcher("/", "/ops/**", "/error", "/error/**", "/favicon.ico")
                .authorizeHttpRequests(auth -> auth
                        .requestMatchers("/", "/error", "/error/**", "/favicon.ico").permitAll()
                        .requestMatchers("/ops/login", "/ops/login.html", "/ops/assets/**").permitAll()
                        .requestMatchers("/ops/**").authenticated()
                        .anyRequest().denyAll())
                .formLogin(login -> login
                        .loginPage("/ops/login")
                        .loginProcessingUrl("/ops/login")
                        .defaultSuccessUrl("/ops/index.html", true)
                        .failureUrl("/ops/login?error=true")
                        .permitAll())
                .logout(logout -> logout
                        .logoutUrl("/ops/logout")
                        .logoutSuccessUrl("/ops/login?logout=true"))
                .csrf(AbstractHttpConfigurer::disable);
        return http.build();
    }

    @Bean
    @Order(3)
    public SecurityFilterChain denyOtherHttp(HttpSecurity http) throws Exception {
        http.authorizeHttpRequests(auth -> auth.anyRequest().denyAll())
                .csrf(AbstractHttpConfigurer::disable);
        return http.build();
    }
}
