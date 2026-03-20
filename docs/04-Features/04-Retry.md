# 消息重传技术规范

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

## 五、参数

| 项目 | 值 |
|------|-----|
| 协议 | TCP + TLS 1.3 + ZSP |
| 重试 | 3次 |
| 超时 | 30秒 |
| 窗口大小 | 64 |
| ACK | ZSP 0x0D |
| RECEIPT | ZSP 0x0C |
| NACK | ZSP 0x0E |
