# ZSP 协议技术规范

## 一、概述

ZSP (Zero Secure Protocol) 是 ZerOS-System 安全即时通讯系统的自定义网络协议，基于 TLV 结构设计，支持端到端加密、文件传输、音视频通话等 IM 特性。

详见 [01-Overview.md](01-Overview.md)。

**读文档顺序建议**：先 **第三节 Header**（含 4 字节 `SessionID`）→ **第五节 MessageType** → **第六节 载荷** → **第七节 TLV**（与 第五节 **不同枚举**）。

---

## 二、数据包结构

```
┌─────────────┬─────────────┬─────────────┬─────────────┐
│   Header    │    Meta     │   Payload   │  Auth Tag   │
│  (16字节)    │   (变长)     │   (变长)    │   (16字节)   │
└─────────────┴─────────────┴─────────────┴─────────────┘
```

---

## 三、协议头 (Header)

**长度：16 字节**

| 偏移 | 字段 | 长度 | 说明 |
|------|------|------|------|
| 0 | Magic | 2 | 协议魔数 `0x5A 0x53` ("ZS") |
| 2 | Version | 1 | 协议版本 (如 0x01) |
| 3 | MessageType | 1 | 消息类型 |
| 4 | Flags | 1 | 标志位 |
| 5 | Reserved | 1 | 保留 |
| 6 | SessionID | 4 | 会话标识 |
| 10 | Sequence | 4 | 序列号 |
| 14 | Length | 2 | Payload 长度 |

### 3.1 标志位 (Flags)

| 位 | 名称 | 说明 |
|----|------|------|
| 0 | Compressed | 1=Payload已压缩 |
| 1 | Encrypted | 1=Payload已加密 (必须) |
| 2 | Priority | 1=高优先级 |
| 3 | MultiDevice | 1=多设备消息 |
| 4-7 | Reserved | 保留 |

---

## 四、元数据 (Meta)

**变长结构**

```
┌───────────────┬───────────────┬───────────────┬───────────────┐
│ MetaLength(2) │  Timestamp(8) │  Nonce(12)    │   KeyID(4)    │
└───────────────┴───────────────┴───────────────┴───────────────┘
```

| 字段 | 长度 | 说明 |
|------|------|------|
| MetaLength | 2 | Meta 总长度 |
| Timestamp | 8 | Unix 毫秒时间戳 |
| Nonce | 12 | 随机数，每次加密唯一 |
| KeyID | 4 | 密钥版本标识 |

---

## 五、消息类型 (MessageType)

**本节**：`Header.MessageType`（1 字节）。**不是** Payload 里 TLV 的 `Type`；与 **第7.2节 TLV** 数值可能相同但语义无关——**易混字节见 第5.1节**。

| Type | 名称 | 说明 |
|------|------|------|
| 0x01 | TEXT | 文本消息 |
| 0x02 | IMAGE | 图片消息 |
| 0x03 | VOICE | 语音消息 |
| 0x04 | VIDEO | 视频消息 |
| 0x05 | FILE_INFO | 文件元数据 |
| 0x06 | FILE_CHUNK | 文件分片 |
| 0x07 | FILE_COMPLETE | 文件传输完成 |
| 0x08 | VOICE_CALL | 语音通话 |
| 0x09 | VIDEO_CALL | 视频通话 |
| 0x0A | CALL_SIGNAL | 呼叫信令 |
| 0x0B | TYPING | 正在输入 |
| 0x0C | RECEIPT | 已读回执 |
| 0x0D | ACK | 送达回执 |
| 0x0E | GROUP_INVITE | 群邀请 |
| 0x0F | GROUP_CREATE | 创建群组 |
| 0x10 | GROUP_UPDATE | 群更新 |
| 0x11 | GROUP_LEAVE | 退出群组 |
| 0x12 | FRIEND_REQUEST | 好友请求 |
| 0x13 | FRIEND_RESPONSE | 好友响应 |
| 0x14 | GROUP_MUTE | 群禁言 |
| 0x15 | GROUP_REMOVE | 群踢人 |
| 0x16 | GROUP_TRANSFER_OWNER | 群主转让 |
| 0x17 | GROUP_JOIN_REQUEST | 入群申请 |
| 0x18 | DELETE_FRIEND | 删除好友 |
| 0x19 | FRIEND_NOTE_UPDATE | 好友备注更新 |
| 0x1A | RESUME_TRANSFER | 文件续传 |
| 0x1B | CANCEL_TRANSFER | 取消传输 |
| 0x1C | GROUP_NAME_UPDATE | 群名称修改 |
| 0x80 | HEARTBEAT | 心跳 |
| 0x81 | AUTH | 认证 |
| 0x82 | LOGOUT | 登出 |
| 0x83 | SYNC | 消息同步 |
| 0xFE | CUSTOM | 自定义消息 |

### 5.1 与 第7.2节 TLV「同字节不同义」（速查）

| 字节 | **MessageType（第五节，在 ZSP 头里）** | **TLV Type（第7.2节，在 Payload 扩展里）** |
|:----:|--------------------------------------|----------------------------------------|
| **0x10** | `GROUP_UPDATE` | `MessageReply` |
| **0x11** | `GROUP_LEAVE` | `MessageRecall` |
| **0x12** | `FRIEND_REQUEST` | `MessageEdit` |
| **0x15** | `GROUP_REMOVE` | `Mention` |

**0x20**：仅出现在 **第7.2节**（`GroupName` TLV）；**第五节** 无对应 `MessageType`。

---

## 六、消息内容结构

### 6.1 TEXT (0x01)

```
┌───────────────┬───────────────┐
│ Content(UTF-8)│  TLV Extension│
└───────────────┴───────────────┘
```

### 6.2 IMAGE / VOICE / VIDEO (0x02-0x04)

```
┌─────────┬─────────┬─────────┬─────────┬─────────┬─────────┐
│FileID(16)│ThumbLen│ Thumb   │ URLLen  │  URL    │Duration │
│         │  (2)    │(变长)    │  (2)    │(变长)    │  (4)    │
└─────────┴─────────┴─────────┴─────────┴─────────┴─────────┘
```

### 6.3 FILE_INFO (0x05)

```
┌─────────────────────────────────────────────────────────────────┐
│ FileID(16)│FileNameLen│FileName│FileSize(8)│MimeTypeLen│MimeType│
├───────────┼───────────┼────────┼───────────┼───────────┼────────┤
│ SHA256(32)│EncryptKey │Chunksz │TotChunks  │TransMode  │        │
│           │   (32)    │ (4)    │   (4)     │   (1)     │        │
└───────────┴───────────┴────────┴───────────┴───────────┴────────┘
```

| 字段 | 说明 |
|------|------|
| FileID | 文件唯一标识 UUID |
| FileName | 文件名 |
| FileSize | 文件大小 (字节) |
| SHA256 | 文件完整性哈希 |
| EncryptKey | 文件加密密钥 (32字节) |
| ChunkSize | 分片大小 (推荐 65536) |
| TotalChunks |总分片数 |
| TransferMode | 0=直传, 1=服务器中转 |

### 6.4 FILE_CHUNK (0x06)

```
┌───────────────┬─────────────┬─────────────┐
│  FileID(16)   │ ChunkIndex  │  ChunkData  │
│               │    (4)      │   (变长)     │
└───────────────┴─────────────┴─────────────┘
```

### 6.5 FILE_COMPLETE (0x07)

```
┌───────────────┬─────────────┬─────────────┐
│  FileID(16)   │  SHA256(32) │ Status(1)   │
└───────────────┴─────────────┴─────────────┘
```

| Status | 说明 |
|--------|------|
| 0 | 成功 |
| 1 | 校验失败 |

### 6.6 CALL_SIGNAL (0x0A)

| 子类型 | 说明 |
|--------|------|
| 1 | Ringing 振铃 |
| 2 | Accept 接听 |
| 3 | Reject 拒绝 |
| 4 | End 结束 |
| 5 | Busy 忙线 |
| 6 | Timeout 超时 |
| 7 | ICE Candidate |

```
┌───────────────┬─────────────┬─────────────┐
│  SubType(1)   │ Duration(4) │  Data(变长)  │
│               │             │ (SDP/ICE)   │
└───────────────┴─────────────┴─────────────┘
```

### 6.7 AUTH (0x81)

```
┌───────────────┬─────────────┬─────────────┬─────────────┐
│ UserIDLen(2)  │  UserID     │  TokenLen   │   Token     │
│               │   (变长)     │    (2)      │   (变长)     │
└───────────────┴─────────────┴─────────────┴─────────────┘
```

---

## 七、TLV 扩展区

### 7.1 TLV 结构

```
┌────────┬───────────┬─────────────┐
│ Type(1)│ Length(2) │   Value     │
│        │           │   (变长)     │
└────────┴───────────┴─────────────┘
```

### 7.2 标准扩展类型 (0x10-0x7F)

**与 第五节区分**：本节为 **载荷内 TLV 扩展** 的 `Type` 字节；与 **ZSP 消息头 `MessageType`**（第五节）是**两套枚举**，数值**可以相同**（例如 **0x15** 在 第五节为 `GROUP_REMOVE`，在 第七节为 `Mention` TLV）但**不得混用**。

| Type | 名称 | 说明 |
|------|------|------|
| 0x10 | MessageReply | 回复消息ID |
| 0x11 | MessageRecall | 消息撤回 |
| 0x12 | MessageEdit | 消息编辑 |
| 0x13 | MessageReaction | 表情回应 |
| 0x14 | ThreadID | 话题ID |
| 0x15 | Mention | @提及 |
| 0x16 | Forwarded | 转发消息 |
| 0x20 | GroupName | 群名称 |

### 7.3 厂商扩展 (0x80-0xFF)

厂商可自定义扩展。

---

## 八、安全机制

### 8.1 加密

| 算法 | 用途 |
|------|------|
| X25519 | 密钥交换 (DH) |
| AES-256-GCM | 消息加密 |
| Ed25519 | 消息签名 |
| SHA-256 | 数据哈希 |

### 8.2 密钥层级

```
Identity Key (长期) → PreKey Bundle → Session Key → Message Key
```

### 8.3 双棘轮

每条消息使用独立 Message Key，发送后立即更新。
定期执行 DH Ratchet 更新根密钥。

### 8.4 防重放

#### 滑动窗口参数

| 参数 | 值 | 说明 |
|------|-----|------|
| 窗口大小 | 1000条 | 允许的序列号范围 |
| 窗口起始 | CurrentSeq - 500 | 中心对齐 |
| 窗口结束 | CurrentSeq + 500 | 中心对齐 |
| Timestamp窗口 | ±5分钟 | 消息时间检查 |

#### 防重放检查流程

```
接收消息(seq=N):
1. 检查 N 是否在 [CurrentSeq-500, CurrentSeq+500] 范围内
2. 检查 N 是否大于 lastProcessedSeq
3. 检查 N 是否不在 usedNonces 集合中
4. 三项全部通过 → 接受消息
5. 任一项失败 → 拒绝消息
```

#### 窗口滑动规则

当 lastProcessedSeq 更新时:
- 清除 seq < lastProcessedSeq - 500 的 usedNonces
- 保持 lastProcessedSeq - 500 到 lastProcessedSeq + 500 的状态

---

## 九、认证标签 (Auth Tag)

**长度：16 字节**

AEAD 加密输出，包含加密数据完整性认证。

---

## 十、传输层

| 项目 | 要求 |
|------|------|
| 协议 | TCP / UDP |
| 传输加密 | TLS 1.3 (必须) |
| 端口 | 自定义 (默认 8848) |
| 心跳间隔 | 30 秒 |
| 心跳超时 | 90 秒 |

---

## 十一、附录

### 11.1 数据类型长度

| 类型 | 长度 |
|------|------|
| UUID | 16 字节 |
| Timestamp | 8 字节 (uint64) |
| Sequence | 4 字节 (uint32) |
| Nonce | 12 字节 |
| Auth Tag | 16 字节 |
| FileSize | 8 字节 (uint64) |
| Duration | 4 字节 (uint32, 毫秒) |

### 11.2 字段长度定义

| 字段 | 最大长度 |
|------|----------|
| FileName | 255 字节 |
| MimeType | 64 字节 |
| URL | 1024 字节 |
| Token | 4096 字节 |
| Meta | 4096 字节 |
| Payload | 16777216 字节 (16MB) |
