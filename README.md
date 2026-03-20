# <div align="center">ZChat</div>

<div align="center">

**内网安全即时通讯服务**

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Platform](https://img.shields.io/badge/Platform-Windows-blue.svg)]()
[![Security](https://img.shields.io/badge/Security-E2E%20Encryption-green.svg)]()

</div>

---

## 简介

专为**高安全等级内网环境**设计的即时通讯系统，采用端到端加密架构，
消息不在服务器持久化存储，断电即失，最大程度保障数据安全。

> **核心设计理念**: 数据在内存中流转，服务器重启后自动清除，实现"无痕通讯"

---

## 核心特性

### 数据不持久化
> 消息仅存服务器内存，服务重启后自动清除

### 端到端加密
> 所有消息在客户端加密传输，服务器仅转发密文

### 可信区隔离
> 核心加密逻辑运行在 C++ 可信区，与业务层物理隔离

### 多级销毁机制

| 等级 | 方式 | 描述 |
|:----:|------|------|
| Level 1 | 标记释放 | 标记内存为可释放状态 |
| Level 2 | 覆写 | 使用随机数据覆写内存 |
| Level 3 | mlock + 多轮覆写 | 锁定内存并多轮覆写 |

### 密钥动态刷新
> 主密钥、会话密钥定期轮换，降低密钥泄露风险

---

## 技术架构

```
客户端 ───▶ ZSP协议 ───▶ SpringBoot(Netty) ───▶ JNI ───▶ C++(MM1/MM2) ───▶ .zdb文件
                                                    │
                                    ┌───────────────┴───────────────┐
                                    │     不可信区    │    可信区     │
                                    │  • 协议解析     │  • MM1        │
                                    │  • 消息路由     │  • MM2        │
                                    │  • 业务调度     │  • .zdb文件   │
                                    └───────────────┴───────────────┘
```

---

## 安全设计

| 安全类别 | 防护措施 |
|:--------:|----------|
| **传输安全** | TLS 1.3、证书固定、端到端加密、滑动窗口防重放 |
| **存储安全** | 消息存MM2内存、.zdb加密、密钥存MM1可信区 |
| **密钥安全** | 每消息独立密钥、24小时轮换、Level 3销毁 |
| **认证安全** | 限流10次/分钟、封禁1小时、会话30分钟超时 |

### 加密算法套件

```
X25519 (密钥交换) + AES-256-GCM (消息加密) + Ed25519 (签名) + SHA-256 (哈希)
```

---

## 技术栈

<div align="center">

| 层级 | 技术 | 说明 |
|:----:|------|------|
| 后端 | SpringBoot + Netty | 高性能网络通信 |
| 安全层 | C++ (JNI) | 可信执行环境 |
| 存储 | SQLite + .zdb | 安全可靠存储 |
| 协议 | ZSP | 自定义二进制协议 |

</div>

---

## 目录结构

```
ZerOS-Chat/
│
├── README.md
│
├── docs/                       技术规范文档
│   ├── 01-Architecture/         架构文档
│   │   ├── 01-Overview.md      • 系统架构总览
│   │   └── 02-ZSP-Protocol.md  • ZSP协议规范
│   ├── 02-Core/                核心模块
│   │   └── 01-MM1.md           • 安全内存框架
│   ├── 03-Business/             业务模块
│   │   ├── 02-Auth.md           • 认证安全
│   │   ├── 03-Group.md          • 群组密钥
│   │   └── 05-KeyRotate.md      • 密钥刷新
│   ├── 04-Features/             功能规范
│   │   ├── 01-MessageSync.md    • 消息同步
│   │   ├── 02-MessageRecall.md  • 消息撤回
│   │   ├── 05-FriendVerify.md   • 好友验证
│   │   ├── 07-CertPinning.md    • 证书固定
│   │   ├── 09-MessageEdit.md    • 消息编辑
│   │   ├── 10-MessageReply.md   • 消息回复
│   │   └── 11-GroupMute.md      • 群禁言
│   ├── 05-Operations/           运维文档
│   └── 06-Appendix/             附录
│       ├── 01-JNI.md            • JNI接口清单
│       └── 02-Performance.md   • 性能指标
│
└── ZChatIM/                    C++安全模块
```

---

## 文档导航

### 入门指南

| 文档 | 描述 |
|------|------|
| [docs/README.md](docs/README.md) | 技术规范文档索引 |

### 安全机制

| 文档 | 描述 |
|------|------|
| [01-MM1.md](docs/02-Core/01-MM1.md) | 安全内存框架 / 销毁机制 |
| [05-KeyRotate.md](docs/03-Business/05-KeyRotate.md) | 密钥刷新 |
| [07-CertPinning.md](docs/04-Features/07-CertPinning.md) | 证书固定 |
| [02-Auth.md](docs/03-Business/02-Auth.md) | 认证安全 |

### 核心架构

| 文档 | 描述 |
|------|------|
| [01-Overview.md](docs/01-Architecture/01-Overview.md) | 系统架构总览 |
| [02-ZSP-Protocol.md](docs/01-Architecture/02-ZSP-Protocol.md) | ZSP协议规范 |

### 消息功能

| 文档 | 描述 |
|------|------|
| [01-MessageSync.md](docs/04-Features/01-MessageSync.md) | 消息同步 |
| [02-MessageRecall.md](docs/04-Features/02-MessageRecall.md) | 消息撤回 |
| [09-MessageEdit.md](docs/04-Features/09-MessageEdit.md) | 消息编辑 |
| [10-MessageReply.md](docs/04-Features/10-MessageReply.md) | 消息回复 |

### 好友与群组

| 文档 | 描述 |
|------|------|
| [05-FriendVerify.md](docs/04-Features/05-FriendVerify.md) | 好友验证 |
| [03-Group.md](docs/03-Business/03-Group.md) | 群组密钥 |
| [11-GroupMute.md](docs/04-Features/11-GroupMute.md) | 群禁言 |

### 附录

| 文档 | 描述 |
|------|------|
| [01-JNI.md](docs/06-Appendix/01-JNI.md) | JNI接口清单 |
| [02-Performance.md](docs/06-Appendix/02-Performance.md) | 性能指标 |

---

## 快速开始

### 环境要求

| 依赖 | 版本 |
|:----:|-----:|
| CMake | 3.20+ |
| JDK | 17+ |
| Maven | 3.8+ |

### 编译步骤

```bash
# 1. 进入C++模块目录
cd ZChatIM

# 2. 创建并进入构建目录
mkdir build && cd build

# 3. 配置CMake
cmake ..

# 4. 编译项目
cmake --build .
```

---

## 限制声明

<div align="center">

| 限制项 | 说明 |
|:------:|------|
| **适用环境** | 仅限内网环境，不适用于公网部署 |
| **安全假设** | 所有安全机制都基于内网可信环境 |

</div>

---

## 许可证

<div align="center">

Copyright © 2026 **AboutUip**

[![MIT License](https://img.shields.io/badge/License-MIT-brightgreen.svg)](https://opensource.org/licenses/MIT)

</div>
