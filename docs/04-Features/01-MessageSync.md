# 消息同步机制技术规范

> **权威**：**`docs/AUTHORITY.md`**、**`03-Storage.md` 第七节**、**`ZChatIM/docs/Implementation-Status.md`**。  
> **本页**：**ZSP/同步协议与产品策略**；凡与 **「消息是否落盘」** 相关，**以 MM2 实现为准**。

## 一、设计原则

- 无离线/在线消息之别（协议面）
- **客户端 MM2**：**`StoreMessage`** 后 IM 密文在 **进程 RAM（ImRam）**，**进程重启不保留**；**`.zdb` + 元库** 承载**文件分片 / 群友元数据等**（除非清理数据目录或 **`CleanupAllData`**）
- **MM1 会话**：**内存**，重启须 **重新认证**；与上条 **并存**，勿混写「重启消息全丢」
- **服务端/网关** 若为纯内存队列，须在部署文档中**单独声明**；**不得**当作本仓库默认

## 二、架构

```
MM1: 索引层（认证、会话等）
     └── MessageID → {MM2 定位, KeyID, Timestamp}（概念）

MM2: 存储层（当前实现）
     └── **IM**：AES-GCM 密文包 **仅 RAM**；**文件分片等**：**.zdb** 追加 + **`data_blocks`/`zdb_files`** 等元库索引（**`user_version=11`**，**无** `im_messages`）
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

- **产品目标**：热缓存/LRU、全局 7 天等见 **`docs/AUTHORITY.md`**、**`04-Features/README.md`**（**与当前 MM2 RAM IM 分离**）
- **磁盘容量**：受 **`ZDB_FILE_SIZE`、卷数、`ZDB_MAX_WRITE_SIZE`** 约束，见 **`03-Storage.md`** / **`04-ZdbBinaryLayout.md`**

## 七、Android 参考客户端（ZChat）

仓库内 **`Client/Android`** 使用 ZSP **`SYNC`** 与 **`MESSAGE_TEXT`** 拉取/接收明文单聊，**不经过** ZChatIM MM2 落盘路径；本地用 **SQLite**（`ChatMessageDb`）做去重与展示。

**要点**（与 MM2 内存链式同步语义对照）：

- 服务端对会话内消息按 **`after messageId`** 顺序返回增量；消息 **ID 为随机 16 字节**，**不能**用本地插入时间 `ts_ms` 代替「链上最后一条」作为 SYNC 游标。
- 客户端使用 **行自增 `id`** 推导「当前最后/最早一条」的 `messageId`，并配合 **首窗合并、尾部多轮、最早缺口前向补齐** 等策略，减少漏消息与重复轮询带来的滞后。
- 与 **`__ZRTC1__`** 语音信令相关的 **SYNC 重放** 与 **实时 TEXT** 分发顺序，见 **[14-Android-Client-ZChat.md](14-Android-Client-ZChat.md)**。

构建与目录说明见 **`Client/Android/README.md`**。
