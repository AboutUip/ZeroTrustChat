# 消息重传技术规范

> **文档类型**：**ZSP / 传输层** 行为；**不**描述 MM2 落盘或 SQLite。  
> **权威**：**`docs/01-Architecture/02-ZSP-Protocol.md`**；承载层为 **TCP（可选 TLS）+ ZSP 应用帧**，具体栈参数以**实际 Netty/客户端实现**为准。  
> **冲突与落盘**：重传与 **消息是否写入 `.zdb`** 无关；持久化语义见 **`docs/README.md`**「冲突与权威」、**`docs/02-Core/03-Storage.md` 第七节**。

---

## 一、ZSP协议层设计

```
┌─────────────────────────────────────────────────────────────┐
│  ZSP 协议内置重传机制                                          │
├─────────────────────────────────────────────────────────────┤
│  Header.Sequence: 序列号                                     │
│  Header.WindowSize: 窗口大小                                 │
│  MessageType.ACK: 送达回执                                   │
│  MessageType.RECEIPT: 已读回执                               │
└─────────────────────────────────────────────────────────────┘
```

---

## 二、重传流程 (ZSP层)

```
发送方:
1. 构造 ZSP 包 (含 Sequence)
2. 发送
3. 等待 ACK (0x0D)
4. 超时 → 重发相同 Sequence
5. 3次后放弃

接收方:
1. 收到消息
2. 发送 ACK (0x0D)
3. 用户已读 → 发送 RECEIPT (0x0C)
```

---

## 三、防重放 - 滑动窗口

```
滑动窗口机制:
┌─────────────────────────────────────────────────────────────┐
│  窗口范围: [LastAcked+1, LastAcked+WindowSize]               │
├─────────────────────────────────────────────────────────────┤
│  LastAcked: 最后确认的序列号                                   │
│  WindowSize: 窗口大小 (默认 64)                               │
│  超出窗口: 丢弃，发送 NACK                                     │
└─────────────────────────────────────────────────────────────┘

校验:
├── Sequence 在窗口内
├── Nonce 未使用
├── Timestamp < 5分钟
└── 窗口滑动: 收到 ACK 后滑动
```

---

## 四、窗口状态

```
接收方维护:
- ExpectedSequence: 期望下一个序列号
- LastAcked: 最后确认的序列号
- WindowBuffer: 窗口内缓冲的消息

处理:
1. Sequence == ExpectedSequence: 接受，ExpectedSequence++
2. Sequence > ExpectedSequence: 缓冲，窗口内等待
3. Sequence < ExpectedSequence: 丢弃 (重复)
```

---

## 五、参数

| 项目 | 值 |
|------|-----|
| 协议 | **ZSP**（应用层），下挂 **TCP**；**TLS** 为链路可选加固（见 **`02-ZSP-Protocol.md` 第十节**） |
| 重试 | 3次 |
| 超时 | 30秒 |
| 窗口大小 | 64 |
| ACK | ZSP 0x0D |
| RECEIPT | ZSP 0x0C |
| NACK | ZSP 0x0E |

---

## 六、实现状态

| 层级 | 说明 |
|------|------|
| **ZChatIM C++** | **`ZChatIMCore`** 以 **MM1/MM2 存储与 JNI 头**为主；**不含** Netty/TCP ZSP 栈。重传在 **Java** 侧实现时须与本节及 **`02-ZSP-Protocol.md`** 对齐。 |
| **`ZChatServer`（Java）** | 入站 **`ZspFrameDecoder`**；出站 **`ZspFrameWireEncoder`** → `ByteBuf`；调度 **`ZspMessageDispatcher`**。详见 **`docs/03-Business/01-SpringBoot.md`** §3.1 / §4。 |
| **联调** | JNI 业务入口见 **`docs/06-Appendix/01-JNI.md`**；**`ZChatIM/jni/ZChatIMJNI.cpp`** 在 **`JNI_OnLoad`** 中调用 **`zchatim_RegisterNatives`** 绑定 **`JniNatives.cpp`**（与 **`ZChatIM/docs/Implementation-Status.md`** 为准）。 |

---

## 七、相关文档

| 文档 | 用途 |
|------|------|
| [02-ZSP-Protocol.md](../01-Architecture/02-ZSP-Protocol.md) | 消息类型、头与 TLV 区 |
| [01-SpringBoot.md](../03-Business/01-SpringBoot.md) | 服务端 Netty 职责 |
| [README.md](../README.md) | 冲突与权威（落盘 vs 内存） |
