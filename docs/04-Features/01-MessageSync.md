# 消息同步机制技术规范

> **权威**：**`docs/README.md`**「冲突与权威」、**`03-Storage.md` 第七节**、**`05-ZChatIM-Implementation-Status.md`**。  
> **本页**：**ZSP/同步协议与产品策略**；凡与 **「消息是否落盘」** 相关，**以 MM2 实现为准**。

## 一、设计原则

- 无离线/在线消息之别（协议面）
- **客户端 MM2**：**`StoreMessage`** 后消息在 **`.zdb` + SQLite**，**进程重启不丢**（除非清理数据目录或 **`CleanupAllData`**）
- **MM1 会话**：**内存**，重启须 **重新认证**；与上条 **并存**，勿混写「重启消息全丢」
- **服务端/网关** 若为纯内存队列，须在部署文档中**单独声明**；**不得**当作本仓库默认

## 二、架构

```
MM1: 索引层（认证、会话等）
     └── MessageID → {MM2 定位, KeyID, Timestamp}（概念）

MM2: 存储层（当前实现）
     └── .zdb 尾部追加 opaque 块 + SQLite 索引；消息体为 AES-GCM 密文包（Windows）
```

## 三、同步流程

```
客户端连接:
1. ZSP握手
2. 客户端发送 SYNC(lastMsgId / timestamp)
3. 对端从存储拉取增量（服务端模型）；本地已落盘消息由 MM2 直接可读
4. 依次推送
5. 7 天策略：产品级保留期；当前 C++ **不会**因日期自动删 `.zdb` 消息（见 02-MM2 第六节）
```

## 四、消息结构（磁盘 opaque 包）

**勿使用下列旧示意图**（与实现不符）。**当前 MM2** 写入 `.zdb` 的载荷为：

**`nonce(12) ‖ ciphertext ‖ auth_tag(16)`**（AES-256-GCM，密钥 **`mm2_message_key.bin`**），与 **`message_id`** 对应 **`data_blocks`（chunk_idx=0）**。

详见 **`docs/02-Core/03-Storage.md` 第三节 / 第七节**、**`04-ZdbBinaryLayout.md`** 第3节。

## 五、服务重启（ZChatIM 客户端）

| 数据 | 保留 | 丢失 |
|------|------|------|
| `.zdb` 与 `zchatim_metadata.db` 上的消息/文件分片 | ✓ | - |
| MM1 会话 / 限流等 | - | ✓ |
| 协议 Session Key（内存） | - | ✓（须重协商，见 KeyRotate） |

## 六、容量

- **产品目标**：单会话热缓存 100 条、全局 7 天等见 **`03-MessageCache.md`**（**与当前 MM2 持久化模块分离**）
- **磁盘容量**：受 **`ZDB_FILE_SIZE`、卷数、`ZDB_MAX_WRITE_SIZE`** 约束，见 **`03-Storage.md`** / **`04-ZdbBinaryLayout.md`**
