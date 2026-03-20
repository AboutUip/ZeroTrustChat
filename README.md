# ZChat

<div align="center">

[![License](https://img.shields.io/badge/license-Apache%202.0-blue.svg)](LICENSE)
[![Language](https://img.shields.io/badge/language-C%2B%2B%20%2B%20SpringBoot-yellow.svg)]()
[![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20Windows-green.svg)]()

</div>

> 专为极限数据安全实现的 IM 通信服务，宁可销毁不留痕，内网 IM 安全方案。

## 核心特性

<div align="center">

| 特性 | 描述 |
|:---:|:---|
| 🔐 端到端加密 | 消息在传输和存储过程中全程加密 |
| 🗑️ 安全销毁 | 多级数据销毁机制，确保敏感信息不留痕迹 |
| 🛡️ 可信区隔离 | C++ 可信区处理核心安全逻辑，与不可信区分离 |
| 🌐 内网部署 | 专为内网环境设计，极致数据安全 |

</div>

## 技术架构

```
┌─────────┐      ZSP       ┌─────────────┐      JNI       ┌────────┐
│  客户端  │ ───────────> │  SpringBoot │ ───────────> │   C++   │
│         │ <──────────  │  (Netty)    │ <────────── │ (MM1/MM2)│
└─────────┘               └─────────────┘               └────┬─────┘
                                                              │
                                                              ▼
                                                     ┌────────────────┐
                                                     │   data/        │
                                                     │   *.zdb (500KB)│
                                                     └────────────────┘
```

### 模块划分

| 区域 | 技术栈 | 职责 |
|:---|:---|:---|
| 不可信区 | SpringBoot (Netty) | ZSP 协议编解码、消息路由、业务调度、JNI 调用 |
| 可信区 | C++ (MM1/MM2) | 消息加密/解密、密钥管理、.zdb 文件存储、安全销毁 |

## 技术栈

- **后端框架**：SpringBoot (Netty)
- **核心模块**：C++ (JNI)
- **数据库**：SQLite
- **文件存储**：自研 .zdb 文件系统
- **通信协议**：ZSP 自研协议

## 安全理念

> **宁可销毁不留痕**

ZChat 始终将数据安全放在首位，通过以下机制确保敏感信息在任何情况下都不会泄露：

- **多级销毁**：标记释放 → 覆写 → mlock + 多轮覆写
- **密钥管理**：Master Key + 文件加密密钥 + 用户长期密钥，24小时刷新
- **数据分类**：内存数据 7 天过期，会话密钥 24 小时过期
- **触发条件**：登出、会话结束、检测调试、异常信号

## 快速开始

### 构建要求

**C++ 模块**
- CMake 3.20+
- Visual Studio 2022 / GCC 11+
- C++17

**SpringBoot 模块**
- JDK 17+
- Maven 3.8+

### 编译

```bash
# C++ 模块
cd ZChatIM
mkdir build && cd build
cmake ..
cmake --build .

# SpringBoot 模块 (待实现)
```

## 安全声明

⚠️ 本项目专为内网环境设计，不适用于公网部署。所有安全机制都基于内网可信环境的假设设计。

## 许可证

[![License](https://img.shields.io/badge/license-Apache%202.0-blue.svg)](LICENSE)

Copyright (c) 2026 [小萱baibai (AboutUip)](https://github.com/AboutUip)
