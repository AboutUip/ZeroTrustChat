# SpringBoot 技术规范

> **仓库边界**：**`ZChatIM/`** 提供 **MM1/MM2 + JNI 头 + 桩 `ZChatIMJNI.cpp`**；**SpringBoot/Netty** 工程若在本 monorepo 外，须与本节及 **`02-ZSP-Protocol.md`** 对齐。  
> **JNI 契约**：**不得以本节 第五节 简表替代** **`docs/06-Appendix/01-JNI.md`**（含 **`callerSessionId` 首参**、`imSessionId` 与 **`StoreMessage`** 参数名）。  
> **落盘**：消息持久化由 **`MM2::StoreMessage`** 等完成（**`docs/README.md`**「冲突与权威」）；SpringBoot **不直连** SQLite/`.zdb` 文件。

## 一、职责

- ZSP 协议服务端
- 业务逻辑调度
- 消息路由
- 对外 HTTP API

**原则**：不处理任何安全相关数据，仅持有引用 ID；**Payload opaque 透传** 至 JNI。

详见 [01-Overview.md](../01-Architecture/01-Overview.md)

---

## 二、网络架构

```
┌─────────┐      ZSP      ┌─────────────┐
│  客户端  │ ────────────> │ SpringBoot  │
│         │ <───────────  │  (Netty)    │
└─────────┘               └──────┬──────┘
                                 │
                                 │ JNI
                                 ▼
                         ┌─────────────┐
                         │     C++     │
                         │   (MM1/MM2) │
                         └─────────────┘
```

---

## 三、模块划分

### 3.1 ZSP Server

| 模块 | 说明 |
|------|------|
| ZSPDecoder | 协议解码 |
| ZSPEncoder | 协议编码 |
| ZSPHandler | 消息分发 |
| HeartbeatHandler | 心跳检测 |

### 3.2 Business

| 模块 | 说明 |
|------|------|
| ConnectionManager | 连接管理 |
| MessageRouter | 消息路由 |
| SessionManager | 会话管理 (仅存 ID) |
| UserManager | 用户管理 |

### 3.3 JNI Client

| 模块 | 说明 |
|------|------|
| NativeInterface | JNI 调用封装 |
| ResultHandler | 结果处理 |

---

## 四、消息处理

### 4.1 接收消息

```
1. Netty 接收 ZSP 数据
2. ZSPDecoder 解析 Header + Meta
3. 获取 MessageType、ZSP 层 SessionID（4B 头字段，与 imSessionId 16B 不同，见 01-JNI.md）
4. 调用 JNI: 已认证上下文中 storeMessage(caller, imSessionId, payload)（完整签名见 01-JNI.md）
5. 获取 msgId
6. 路由发送
```

### 4.2 发送消息

```
1. 根据连接查找目标 Channel
2. 调用 JNI: retrieveMessage(caller, messageId)（或 getSessionMessages 等）
3. 获取 opaque 载荷
4. ZSPEncoder 编码
5. 发送
```

---

## 五、JNI 接口

| 接口 | 输入 | 输出 | 说明 |
|------|------|------|------|
| `auth` | userId, token, clientIp(可选) | sessionId/false | 认证；IP 建议透传至 JNI 以满足 02-Auth |
| `storeMessage` | sessionId, data | msgId/null | 存储消息 |
| `retrieveMessage` | msgId | data/null | 获取消息 |
| `getSessionStatus` | sessionId | active/invalid | 会话状态 |
| `destroySession` | sessionId | true/false | 销毁会话 |
| `getUserStatus` | userId | online/offline | 用户状态 |
| `emergencyWipe` | - | - | 紧急销毁 |

---

## 六、安全原则

| 禁止事项 | 说明 |
|----------|------|
| 不存储消息明文 | 仅透传 |
| 不处理加解密 | 由 C++ 处理 |
| 不存储密钥 | 不获取不存储 |
| 不解析业务数据 | Payload 原文传递 |

---

## 七、错误处理

| 场景 | 处理 |
|------|------|
| JNI 调用失败 | 返回错误码 |
| sessionId 无效 | 断开连接 |
| 目标不在线 | **离线队列 / 推送**（产品）；**非**「SpringBoot 写 MM2」——持久化仅在 **native MM2** 路径完成 |
| 超时 | 重试/断开 |

---

## 八、日志

- 操作日志 (连接/断开/消息路由)
- 错误日志 (业务异常)
- **不记录**: 消息内容、密钥、用户敏感信息
