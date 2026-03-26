package com.ztrust.zchat.boot;

import jakarta.servlet.FilterChain;
import jakarta.servlet.ServletException;
import jakarta.servlet.http.HttpServletRequest;
import jakarta.servlet.http.HttpServletResponse;
import java.io.IOException;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.core.Ordered;
import org.springframework.core.annotation.Order;
import org.springframework.stereotype.Component;
import org.springframework.web.filter.OncePerRequestFilter;

/**
 * 将每条 HTTP 请求的方法、路径与响应状态打到<strong>应用日志</strong>（控制台）。
 * Tomcat {@code accesslog} 默认写入工作目录下 {@code logs/}，在 IDE 里常误以为「没有请求日志」。
 */
@Component
@Order(Ordered.HIGHEST_PRECEDENCE)
public class HttpRequestLogFilter extends OncePerRequestFilter {

    private static final Logger log = LoggerFactory.getLogger(HttpRequestLogFilter.class);

    @Override
    protected void doFilterInternal(HttpServletRequest request, HttpServletResponse response, FilterChain filterChain)
            throws ServletException, IOException {
        filterChain.doFilter(request, response);
        if (log.isInfoEnabled()) {
            log.info("{} {} -> {}", request.getMethod(), request.getRequestURI(), response.getStatus());
        }
    }
}
