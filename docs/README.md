# 技术规范文档

本文档包含 ZChatIM 系统的完整技术规范。

## 文档层级

### 01-Architecture - 架构文档

| 文档 | 说明 |
|------|------|
| [01-Overview](01-Architecture/01-Overview.md) | 系统架构总览 |
| [02-ZSP-Protocol](01-Architecture/02-ZSP-Protocol.md) | ZSP协议规范 |

### 02-Core - 核心模块

| 文档 | 说明 |
|------|------|
| [01-MM1](02-Core/01-MM1.md) | 安全内存框架 |
| [02-MM2](02-Core/02-MM2.md) | 消息加密存储 |
| [03-Storage](02-Core/03-Storage.md) | 存储机制 |

### 03-Business - 业务模块

| 文档 | 说明 |
|------|------|
| [01-SpringBoot](03-Business/01-SpringBoot.md) | SpringBoot服务 |
| [02-Auth](03-Business/02-Auth.md) | 认证安全 |
| [03-Group](03-Business/03-Group.md) | 群组密钥 |
| [04-Session](03-Business/04-Session.md) | 会话管理 |
| [05-KeyRotate](03-Business/05-KeyRotate.md) | 密钥刷新 |
| [06-AccountDelete](03-Business/06-AccountDelete.md) | 账户注销 |

### 04-Features - 功能规范

| 文档 | 说明 |
|------|------|
| [01-MessageSync](04-Features/01-MessageSync.md) | 消息同步 |
| [02-MessageRecall](04-Features/02-MessageRecall.md) | 消息撤回 |
| [03-MessageCache](04-Features/03-MessageCache.md) | 消息缓存 |
| [04-Retry](04-Features/04-Retry.md) | 重传机制 |
| [05-FriendVerify](04-Features/05-FriendVerify.md) | 好友验证 |
| [06-MultiDevice](04-Features/06-MultiDevice.md) | 多设备 |
| [07-CertPinning](04-Features/07-CertPinning.md) | 证书固定 |
| [08-FileTransfer](04-Features/08-FileTransfer.md) | 文件传输 |
| [09-MessageEdit](04-Features/09-MessageEdit.md) | 消息编辑 |
| [10-MessageReply](04-Features/10-MessageReply.md) | 消息回复 |
| [11-GroupMute](04-Features/11-GroupMute.md) | 群禁言 |
| [12-Mention](04-Features/12-Mention.md) | @提及 |
| [13-GroupName](04-Features/13-GroupName.md) | 群名称 |

### 05-Operations - 运维文档

| 文档 | 说明 |
|------|------|
| [01-Backup](05-Operations/01-Backup.md) | 灾备恢复 |

### 06-Appendix - 附录

| 文档 | 说明 |
|------|------|
| [01-JNI](06-Appendix/01-JNI.md) | JNI 接口清单（**`C++` 列**与 `JniInterface.h` / `JniBridge.h` 严格一一对应；业务 API 首参 **`callerSessionId`**，见表首说明） |
| [02-Performance](06-Appendix/02-Performance.md) | 性能指标 |
| [03-Version](06-Appendix/03-Version.md) | 版本兼容 |

> **JNI 详细版（路由与安全不变量）**：见仓库内 [`ZChatIM/docs/JNI-API-Documentation.md`](../ZChatIM/docs/JNI-API-Documentation.md)，须与上表及头文件同步维护。

**实现索引（C++）**：认证限流/封禁见 [`03-Business/02-Auth.md`](03-Business/02-Auth.md) §七；IM 会话 idle/`lastActive` 见 [`03-Business/04-Session.md`](03-Business/04-Session.md) §七。

## 快速索引

### 安全机制

- 销毁级别: [02-Core/01-MM1.md](02-Core/01-MM1.md)
- 密钥刷新: [03-Business/05-KeyRotate.md](03-Business/05-KeyRotate.md)
- 证书固定: [04-Features/07-CertPinning.md](04-Features/07-CertPinning.md)

### 消息功能

- 消息同步: [04-Features/01-MessageSync.md](04-Features/01-MessageSync.md)
- 消息撤回: [04-Features/02-MessageRecall.md](04-Features/02-MessageRecall.md)
- 消息编辑: [04-Features/09-MessageEdit.md](04-Features/09-MessageEdit.md)

### 好友与群组

- 好友验证: [04-Features/05-FriendVerify.md](04-Features/05-FriendVerify.md)
- 群组密钥: [03-Business/03-Group.md](03-Business/03-Group.md)
- 群禁言: [04-Features/11-GroupMute.md](04-Features/11-GroupMute.md)
