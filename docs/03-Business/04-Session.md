# 会话管理技术规范

## 一、会话生命周期

```
登录 → 会话建立 → 活跃状态 → idle → 超时销毁
```

## 二、会话超时

| 项目 | 值 |
|------|-----|
| idle超时 | 30分钟 |
| 心跳间隔 | 30秒 |
| 心跳超时 | 90秒 |
| 存储位置 | MM1 内存 |

## 三、超时处理

```
会话超时:
1. MM1 检测 idle 时间 > 30分钟
2. 执行 Level 2 销毁
3. 通知客户端连接断开
4. 释放 sessionId 资源

客户端:
1. 检测连接断开
2. 自动重连
3. 重新认证
```

## 四、心跳保活

```
客户端 → 服务器:
1. 每30秒发送 HEARTBEAT(0x80)
2. 服务器更新 lastActive 时间
3. 90秒无心跳 → 判定超时
4. 执行会话销毁
```

## 五、登出 vs 超时

| 场景 | 行为 |
|------|------|
| 主动登出 | 发送 LOGOUT → Level 2 销毁 → 客户端清除密钥 |
| 超时销毁 | 服务器主动断开 → Level 2 销毁 → 客户端重连 |

## 六、安全保证

- 会话存储: MM1 内存
- 销毁级别: Level 2
- 服务重启: 会话丢失
- 无日志记录

## 七、C++ 实现落点（`ZChatIM`）

| 组件 | 路径 | 说明 |
|------|------|------|
| `SessionActivityManager` | `include/mm1/managers/SessionActivityManager.h`、`src/mm1/managers/SessionActivityManager.cpp` | 维护 **imSessionId（16 字节）** 的 `lastActive`（Unix 纪元毫秒）；**idle 30 分钟** 与 §二 一致；`TouchSession`/`CleanupExpiredSessions`/`IsSessionExpired` 对不可信 `nowMs` 相对本机时间做**钳制**（防止恶意超前时间延长 idle）；`GetSessionStatus` 使用本机当前 Unix 毫秒。 |

> **与 ZSP**：此处 `imSessionId` 为 JNI/MM1 通道会话标识，**不是** ZSP Header 中 4 字节 `SessionID`（见 `Types.h` / `02-ZSP-Protocol.md`）。
