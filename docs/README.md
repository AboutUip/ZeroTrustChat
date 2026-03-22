# 技术文档索引

本目录为 **ZerOS-Chat / ZChatIM** 产品级技术规范的权威来源，与 **`ZChatIM/`** 源码中的头文件、CMake 定义共同构成实现契约。

---

## 1. 适用范围与读者

| 读者 | 建议重点 |
|------|----------|
| 实现与评审（C++ / JNI） | **第2节** 权威链、**`02-Core/`**、**`06-Appendix/01-JNI.md`**、**`07-Engineering/`** |
| 协议与服务（Java / Spring） | **`01-Architecture/`**、**`03-Business/01-SpringBoot.md`** |
| 功能与业务规则 | **`04-Features/`**、**`03-Business/`**（与 **`05-ZChatIM-Implementation-Status.md`** 对照实现状态） |

---

## 2. 规范性引用与冲突处理

编写或评审 **`04-Features/`**、**`03-Business/`**、**`01-Architecture/`** 时，凡涉及 **消息/文件是否持久化**、**进程重启后本地数据是否保留**，须与下表一致：

| 优先级 | 来源 | 作用 |
|:------:|------|------|
| **1** | **`ZChatIM/` 源码**（`MM2.cpp`、`SqliteMetadataDb`、`ZdbManager` 等） | 运行行为以编译产物为准 |
| **2** | **[05-ZChatIM-Implementation-Status.md](02-Core/05-ZChatIM-Implementation-Status.md)** | 已实现 / 桩 / 未接入的集中说明 |
| **3** | **[03-Storage.md](02-Core/03-Storage.md) 第七节**、[04-ZdbBinaryLayout.md](02-Core/04-ZdbBinaryLayout.md) | 元数据与 **`.zdb` v1** 物理语义 |

**已对齐结论（摘要）**：

- **IM 消息**：**`StoreMessage`** 将 **AES-GCM** 密文写入 **`.zdb`**（**全平台 OpenSSL 3**；磁盘格式一致），**`im_messages` + `data_blocks`** 位于 **元数据 SQLite**（**默认 `ZCHATIM_USE_SQLCIPHER`**：**SQLCipher** 页加密，见 **`03-Storage.md` §4.2**）；进程重启后数据**默认保留**，除非执行 **`Cleanup` / `CleanupAllData`** 或删除数据目录。
- **文件分片**：**`StoreFileChunk`** 同样写入 **`.zdb`** 并登记索引；与「仅内存缓存」类叙述冲突时，**以本仓库实现与本文第3节为准**。
- **续传、好友请求、群显示名、群禁言（`mm2_group_mute`）、回复/编辑元数据、MM1/JNI UserData（`mm1_user_kv`）等**：由 **MM2 + `SqliteMetadataDb`（当前 `user_version=6`）** 持久化（见 **`03-Storage.md`**、**`05` 第2.1节**）。**群基础**（**`createGroup`/`inviteMember`/`…`/`updateGroupKey`**）已 **`mm1::GroupManager` 实装**（**`ZGK1`** 等）；**`updateGroupName`** 已 **`GroupNameManager`**；**`muteMember`/`isMuted`/`unmuteMember`** 已 **`GroupMuteManager`**（见 **`05` §3**）；**其余 JNI 业务**仍可能为桩：桥接层须遵守 **`01-JNI.md`** 中 MM1 校验再落 MM2 等约定。
- **MM1 会话表、认证限流等**：以各业务文档为准，多为**进程内状态**，与 MM2 磁盘态**并存、不可混用叙述**。

旧资料中「服务重启导致聊天消息丢失」仅适用于**无本地 MM2 持久化**的部署模型；**默认指本仓库客户端实现时，该结论不成立**。

---

## 3. 目录结构

### 3.1 `01-Architecture` — 架构

| 文档 | 内容 |
|------|------|
| [01-Overview](01-Architecture/01-Overview.md) | 系统架构总览 |
| [02-ZSP-Protocol](01-Architecture/02-ZSP-Protocol.md) | ZSP 协议 |

### 3.2 `02-Core` — 核心实现契约

| 文档 | 内容 |
|------|------|
| [01-MM1](02-Core/01-MM1.md) | MM1 安全内存与销毁级别 |
| [02-MM2](02-Core/02-MM2.md) | MM2 消息与存储编排（概念） |
| [03-Storage](02-Core/03-Storage.md) | 存储模型、表结构、与实现对照 |
| [04-ZdbBinaryLayout](02-Core/04-ZdbBinaryLayout.md) | **`.zdb` v1** 二进制布局 |
| [05-ZChatIM-Implementation-Status](02-Core/05-ZChatIM-Implementation-Status.md) | C++ 实现状态与风险（活文档） |

### 3.3 `03-Business` — 业务与安全策略

| 文档 | 内容 |
|------|------|
| [01-SpringBoot](03-Business/01-SpringBoot.md) | Spring / 网关职责边界 |
| [02-Auth](03-Business/02-Auth.md) | 认证与限流 |
| [03-Group](03-Business/03-Group.md) | 群组密钥 |
| [04-Session](03-Business/04-Session.md) | 会话管理 |
| [05-KeyRotate](03-Business/05-KeyRotate.md) | 密钥轮换 |
| [06-AccountDelete](03-Business/06-AccountDelete.md) | 账户注销 |

### 3.4 `04-Features` — 功能规范

各篇文首应标明与实现、JNI 的对照关系；协议数值以 **`02-ZSP-Protocol.md`** 为准。

| 文档 | 内容 |
|------|------|
| [01-MessageSync](04-Features/01-MessageSync.md) | 消息同步 |
| [02-MessageRecall](04-Features/02-MessageRecall.md) | 消息撤回 |
| [03-MessageCache](04-Features/03-MessageCache.md) | 消息缓存 |
| [04-Retry](04-Features/04-Retry.md) | 重传 |
| [05-FriendVerify](04-Features/05-FriendVerify.md) | 好友验证 |
| [06-MultiDevice](04-Features/06-MultiDevice.md) | 多设备 |
| [07-CertPinning](04-Features/07-CertPinning.md) | 证书固定 |
| [08-FileTransfer](04-Features/08-FileTransfer.md) | 文件传输 |
| [09-MessageEdit](04-Features/09-MessageEdit.md) | 消息编辑 |
| [10-MessageReply](04-Features/10-MessageReply.md) | 消息回复 |
| [11-GroupMute](04-Features/11-GroupMute.md) | 群禁言 |
| [12-Mention](04-Features/12-Mention.md) | @ 提及 |
| [13-GroupName](04-Features/13-GroupName.md) | 群名称 |

### 3.5 `05-Operations` — 运维

| 文档 | 内容 |
|------|------|
| [01-Backup](05-Operations/01-Backup.md) | 备份与恢复 |

### 3.6 `06-Appendix` — 附录

| 文档 | 内容 |
|------|------|
| [01-JNI](06-Appendix/01-JNI.md) | JNI 接口表（与 **`JniInterface.h` / `JniBridge.h`** 一一对应） |
| [02-Performance](06-Appendix/02-Performance.md) | 性能指标 |
| [03-Version](06-Appendix/03-Version.md) | 版本与兼容 |

**JNI 细则（路由、不变量、按 API 说明）**：[ZChatIM/docs/JNI-API-Documentation.md](../ZChatIM/docs/JNI-API-Documentation.md)。修改契约时须与 **`01-JNI.md`**、上述头文件**同步**。

### 3.7 `07-Engineering` — 构建

| 文档 | 内容 |
|------|------|
| [01-Build-ZChatIM](07-Engineering/01-Build-ZChatIM.md) | 依赖、平台差异、Release 配置 |

---

## 4. 建议阅读路径

1. 本文 **第2节** 与 **[05-ZChatIM-Implementation-Status.md](02-Core/05-ZChatIM-Implementation-Status.md)** 内「如何阅读」。
2. 存储与容器：**[03-Storage.md](02-Core/03-Storage.md) 第七节**、[04-ZdbBinaryLayout.md](02-Core/04-ZdbBinaryLayout.md)。
3. 网络与 TLV：**[02-ZSP-Protocol.md](01-Architecture/02-ZSP-Protocol.md)**。
4. JNI：**[01-JNI.md](06-Appendix/01-JNI.md)** 与 **`ZChatIM/docs/JNI-API-Documentation.md`**。
5. 构建：**[07-Engineering/01-Build-ZChatIM.md](07-Engineering/01-Build-ZChatIM.md)**。

**实现索引（节选）**：认证限流 / 封禁见 **[02-Auth.md](03-Business/02-Auth.md) 第七节**；IM 会话 idle / `lastActive` 见 **[04-Session.md](03-Business/04-Session.md) 第七节**。

---

## 5. 维护约定

- 章节引用统一为 **「第×节」** 或 **「《文档名》第×节」**；**勿使用** `§` 符号。
- 功能或接口变更时：**规范文档**、**`05` 实现状态**、**相关头文件**、（若涉及）**JNI 双文档**须同步更新。
- 不在 **`04-Features`** 中重复粘贴与 **`02-Core`** 矛盾的存储实现细节；以 **第2节 权威链** 为准。
- **`ZChatIM/docs/BUILD-WINDOWS.md`** 仅为指向 **`07-Engineering/01-Build-ZChatIM.md`** 的迁移桩，勿在其内扩充正文。

---

## 6. 主题索引

| 主题 | 文档 |
|------|------|
| 销毁级别 | [02-Core/01-MM1.md](02-Core/01-MM1.md) |
| 密钥轮换 | [03-Business/05-KeyRotate.md](03-Business/05-KeyRotate.md) |
| 证书固定 | [04-Features/07-CertPinning.md](04-Features/07-CertPinning.md) |
| 消息同步 / 撤回 / 编辑 | [04-Features/01-MessageSync.md](04-Features/01-MessageSync.md)、[02-MessageRecall.md](04-Features/02-MessageRecall.md)、[09-MessageEdit.md](04-Features/09-MessageEdit.md) |
| 好友 / 群组 | [04-Features/05-FriendVerify.md](04-Features/05-FriendVerify.md)、[03-Business/03-Group.md](03-Business/03-Group.md)、[04-Features/11-GroupMute.md](04-Features/11-GroupMute.md) |
