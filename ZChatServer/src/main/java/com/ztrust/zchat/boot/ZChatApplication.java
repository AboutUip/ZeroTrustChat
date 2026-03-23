package com.ztrust.zchat.boot;

import org.springframework.boot.SpringApplication;
import org.springframework.boot.autoconfigure.SpringBootApplication;

@SpringBootApplication(scanBasePackages = "com.ztrust.zchat")
public class ZChatApplication {

    public static void main(String[] args) {
        SpringApplication.run(ZChatApplication.class, args);
    }
}
