# ZChatIM 技术方案文档

## 一、架构

```
┌─────────┐      ZSP     ┌─────────────┐      JNI      ┌──────────┐
│  客户端  │ ───────────> │  SpringBoot │ ───────────>  │   C++    │
│         │ <──────────  │  (Netty)    │ <──────────   │ (MM1/MM2)│
└─────────┘              └─────────────┘               └────┬─────┘
                                                            │
                                                            ▼
                                                     ┌────────────────┐
                                                     │   data/        │
                                                     │   *.zdb (5MB)  │
                                                     └────────────────┘
```

---

## 二、模块职责

### SpringBoot (不可信区)

| 模块 | 说明 | 详见 |
|------|------|------|
| ZSP编解码 | 协议解析 | [02-ZSP-Protocol.md](02-ZSP-Protocol.md) |
| 消息路由 | 消息转发 | [01-SpringBoot.md](../03-Business/01-SpringBoot.md) |
| 业务调度 | 调用JNI | [01-SpringBoot.md](../03-Business/01-SpringBoot.md) |

### C++ (可信区)

| 模块 | 说明 | 详见 |
|------|------|------|
| MM1 | 安全内存框架 | [01-MM1.md](../02-Core/01-MM1.md) |
| MM2 | 消息加密存储 | [02-MM2.md](../02-Core/02-MM2.md) |
| 密钥管理 | 密钥生命周期 | [05-KeyRotate.md](../03-Business/05-KeyRotate.md)、[03-Group.md](../03-Business/03-Group.md) |
| .zdb存储 | 加密文件存储 | [03-Storage.md](../02-Core/03-Storage.md) |
| 安全销毁 | 三级销毁机制 | [01-MM1.md](../02-Core/01-MM1.md) |

---

## 三、存储机制

### .zdb 文件

| 项目 | 值 |
|------|-----|
| 目录 | data/ |
| 单文件大小 | 5MB（**`ZDB_FILE_SIZE`**，与 `Types.h` 一致） |
| 单次写入 | ≤500KB（**`ZDB_MAX_WRITE_SIZE`**） |
| 填充 | 创建时 payload 区预填高熵字节（见 **`04-ZdbBinaryLayout.md`**） |
| 写入 | **v1：各卷 `usedSize` 处尾部追加**；载荷为分片明文或 **`StoreMessage`** 的 **AES-GCM 密文包**（容器层不加密）。**非**「随机洞位」。详见 [04-ZdbBinaryLayout.md](../02-Core/04-ZdbBinaryLayout.md)、[03-Storage.md](../02-Core/03-Storage.md) 第六节 / 第七节 |

详见 [03-Storage.md](../02-Core/03-Storage.md)

### SQLite

| 表 | 用途 |
|---|------|
| zdb_files | 文件索引 |
| data_blocks | 数据块索引 |
| user_data | 用户数据索引 |
| group_data | 群组数据索引 |
| group_members | 群成员关系 |

详见 [03-Storage.md](../02-Core/03-Storage.md)

---

## 四、数据分类

| 数据 | 持久化 | 过期 | 存储 |
|------|--------|------|------|
| 用户元数据 | .zdb | 不过期 | MM1索引 + .zdb |
| 好友列表 | .zdb | 不过期 | MM1索引 + .zdb |
| 群组信息 | .zdb | 不过期 | MM1索引 + .zdb |
| 聊天消息 | **`.zdb` 密文 + SQLite 索引**（**`StoreMessage`**） | 7 天（**产品 TTL 策略**，**非**当前 MM2 自动裁剪） | MM2（落盘）+ MM1（认证等） |
| 文件分片 | **`.zdb` + `data_blocks`**（**`StoreFileChunk`**） | 产品策略可定清理 | MM2（落盘）+ MM1（按需） |

> **禁止混淆**：**MM1 会话表、限流计数** 等为 **内存**，进程重启即失；**MM2 消息/文件分片** 为 **磁盘**（`dataDir` / `indexDir`），与上表一致。权威见 [docs/README.md](../README.md)「冲突与权威」、[03-Storage.md](../02-Core/03-Storage.md) 第七节、[05-ZChatIM-Implementation-Status.md](../02-Core/05-ZChatIM-Implementation-Status.md)。
| 会话密钥（Session Key，密码学） | 内存 | **1小时**（轮换周期） | MM1 |

> **与 IM 通道**：上表为 **Session Key 轮换/存活策略**（与 [05-KeyRotate.md](../03-Business/05-KeyRotate.md) 第1.1节 一致）。**JNI/MM1 通道** 的 `imSessionId` **idle 30 分钟** 见 [04-Session.md](../03-Business/04-Session.md)，与 Session Key 周期是不同维度。

详见 [01-MessageSync.md](../04-Features/01-MessageSync.md)、[05-KeyRotate.md](../03-Business/05-KeyRotate.md)

---

## 五、安全机制

### 销毁级别

| 级别 | 动作 | 触发 |
|------|------|------|
| Level 1 | 标记释放 | - |
| Level 2 | 覆写 0x00/0xFF/随机数 | 登出/会话结束 |
| Level 3 | mlock + 多轮覆写 + 进程清零 | 检测调试/异常信号 |

详见 [01-MM1.md](../02-Core/01-MM1.md)

### 密钥刷新

**权威**：[05-KeyRotate.md](../03-Business/05-KeyRotate.md)（分级周期、双密钥并行、旧密钥保留 1 小时）。**总览摘要**见本文 **第九节**；群组密钥等业务见 [03-Group.md](../03-Business/03-Group.md)。

---

## 六、认证安全

| 项目 | 规则 |
|------|------|
| IP限流 | 5次/分钟 |
| 用户限流 | 10次/分钟 |
| 封禁 | 连续失败 ≥5 次按矩阵递增（最长 24h） |
| 存储 | MM1内存 |

详见 [02-Auth.md](../03-Business/02-Auth.md)

---

## 七、消息机制

| 功能 | 说明 | 详见 |
|------|------|------|
| 同步 | 统一消息机制 | [01-MessageSync.md](../04-Features/01-MessageSync.md) |
| 撤回 | MM1 Recall + MM2 **区间清零与删索引**（非文件截断） | [02-MessageRecall.md](../04-Features/02-MessageRecall.md) |
| 缓存 | **产品目标** 100 条/会话 LRU；**当前无独立内存缓存模块**，见 [03-MessageCache.md](../04-Features/03-MessageCache.md) |
| 重传 | ZSP协议层 3次重试 | [04-Retry.md](../04-Features/04-Retry.md) |
| 文件传输 | 分片 **已落盘**；**续传/完成/取消** 见 **MM2 + `mm2_file_transfer`**（[05 实现状态](../02-Core/05-ZChatIM-Implementation-Status.md) 第2.1节至第2.2节） | [08-FileTransfer.md](../04-Features/08-FileTransfer.md) |
| 消息编辑 | Ed25519签名验证 | [09-MessageEdit.md](../04-Features/09-MessageEdit.md) |
| 消息回复 | TLV 0x10扩展 | [10-MessageReply.md](../04-Features/10-MessageReply.md) |
| 群组禁言 | 禁言/解禁机制 | [11-GroupMute.md](../04-Features/11-GroupMute.md) |

---

## 八、会话管理

| 项目 | 规则 |
|------|------|
| idle超时 | 30分钟 |
| 心跳间隔 | 30秒 |
| 心跳超时 | 90秒 |
| 存储 | MM1内存 |

详见 [04-Session.md](../03-Business/04-Session.md)

---

## 九、密钥刷新

与 [05-KeyRotate.md](../03-Business/05-KeyRotate.md) **第1.1节** 对齐（**以下表为总览，细节与验收以该文档为准**）：

| 密钥类型 | 轮换周期 |
|----------|:--------:|
| Identity Key | 24小时 |
| Master Key | 6小时 |
| Session Key | 1小时 |
| Message Key | 每条消息 |

| 机制项 | 规则 |
|--------|------|
| 双密钥并行 | 刷新后新旧密钥同时生效（见 05-KeyRotate 第二节） |
| 旧密钥保留 | **1小时**（非 24 小时） |

群组密钥等：**[03-Group.md](../03-Business/03-Group.md)**。

详见 [05-KeyRotate.md](../03-Business/05-KeyRotate.md)

---

## 十、多设备

| 项目 | 规则 |
|------|------|
| 最大设备数 | 2 |
| 踢下线策略 | 强制踢掉最早登录 |
| 消息同步 | 实时广播 |

详见 [06-MultiDevice.md](../04-Features/06-MultiDevice.md)

---

## 十一、账户管理

| 功能 | 说明 |
|------|------|
| 注销 | Level 3 全量销毁 |

详见 [06-AccountDelete.md](../03-Business/06-AccountDelete.md)

---

## 十二、证书固定

| 项目 | 规格 |
|------|------|
| 固定方式 | 公钥哈希 (SPKI SHA-256) |
| 备份数量 | 1个 |
| 轮换 | 自动 |
| 异常封禁 | 连续3次 |

详见 [07-CertPinning.md](../04-Features/07-CertPinning.md)

---

## 十三、服务重启

**ZChatIM 客户端进程（本仓库 C++）**——与 [01-Backup.md](../05-Operations/01-Backup.md) 一致：

```
丢失（内存）: MM1 会话、限流/封禁计数、未持久化的临时态
保留（磁盘）: dataDir 下 .zdb、indexDir 下 zchatim_metadata.db（及 mm2_message_key.bin）— 含已 StoreMessage / StoreFileChunk 的数据
```

**纯网关 / 无本地 MM2 落盘** 的部署另议，须在方案书中单独写清；**不得**默认同上表冲突。

---

## 附录

| 文档 | 详见 |
|------|------|
| JNI接口清单 | [01-JNI.md](../06-Appendix/01-JNI.md) |
| 性能指标 | [02-Performance.md](../06-Appendix/02-Performance.md) |
| 版本兼容 | [03-Version.md](../06-Appendix/03-Version.md) |
