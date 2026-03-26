package com.ztrust.zchat.boot;

import org.springframework.stereotype.Controller;
import org.springframework.web.bind.annotation.GetMapping;

/**
 * 内嵌运维面板入口：{@code /ops} → 静态 {@code /ops/index.html}（与 Actuator 共用 HTTP Basic）。
 */
@Controller
public class OpsDashboardController {

    @GetMapping("/")
    public String root() {
        return "redirect:/ops/index.html";
    }

    @GetMapping("/ops")
    public String opsRoot() {
        return "redirect:/ops/index.html";
    }

    @GetMapping("/ops/login")
    public String opsLogin() {
        return "redirect:/ops/login.html";
    }
}
