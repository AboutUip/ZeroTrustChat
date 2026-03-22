# 多设备登录技术规范

> **文档类型**：**产品与 MM1 会话模型**；与 **MM2 消息落盘** 独立（多设备均可读同一用户本地库或各自客户端库，部署相关）。  
> **权威**：**`docs/03-Business/04-Session.md`**（idle/心跳）；JNI 契约 **`docs/06-Appendix/01-JNI.md`**「八、会话与多设备」。  
> **实现状态**：**`05-ZChatIM-Implementation-Status.md` 第3节**：**JNI** **`registerDeviceSession` / `getDeviceSessions` 等** 已接 **`JniBridge` + `jni/JniNatives.cpp`**；**`DeviceSessionManager`** 当前为 **进程内内存实现**（**`MM1_manager_stubs.cpp`**），**非**持久化/集群级产品形态。

---

## 一、设备管理

```
┌─────────────────────────────────────────────────────────────┐
│  MM1 设备会话表（目标模型）                                    │
├─────────────────────────────────────────────────────────────┤
│  userId → vector<sessionId, deviceId, loginTime>            │
│  最大2个设备                                                  │
└─────────────────────────────────────────────────────────────┘
```

**ID 宽度**：JNI 中 **`deviceId` / `sessionId`（向量）`** 与 **`USER_ID_SIZE` / 16B** 二进制语义一致，**≠** ZSP 头 4 字节 `SESSION_ID_SIZE`（见 **`01-JNI.md`** 文首、`Types.h`）。

---

## 二、登录流程

```
1. 认证成功
2. 检查当前设备数
3. 设备数 < 2 → 允许登录
4. 设备数 = 2 → 踢掉最早登录的设备
5. 新建设备会话
6. 通知被踢设备 → 断开连接
```

---

## 三、会话结构

```
┌─────────────────────────────────────────────────────────────┐
│  sessionId: 会话ID                                           │
│  deviceId: 设备ID (客户端生成UUID)                             │
│  loginTime: 登录时间                                          │
│  lastActive: 最后活跃时间                                     │
└─────────────────────────────────────────────────────────────┘
```

---

## 四、消息同步

```
消息到达:
1. 服务器广播至所有在线设备
2. 各设备独立接收
3. 同步即送达

发送状态:
1. 用户A设备1发送消息
2. 服务器返回 ACK
3. 用户A的所有设备显示"已发送"
4. 对方已读 → 所有设备显示"已读"
```

**与本地存储**：各客户端 **MM2** 可独立 **`StoreMessage`**；**跨设备一致**依赖服务端同步与业务层冲突解决，**非**本页单独规定。

---

## 五、参数

| 项目 | 值 |
|------|-----|
| 最大设备数 | 2 |
| 踢下线策略 | 强制踢掉最早登录 |
| 存储位置 | **MM1 内存**（目标）；进程重启丢失，须重登 |
| 销毁级别 | Level 2 |

---

## 六、JNI 入口（契约）

| C++（`JniInterface`） | 说明 |
|----------------------|------|
| `RegisterDeviceSession` | 注册设备会话；C++ **`bool` + `outKicked`**；Java **`null`**=失败，**`byte[0]`**=成功无踢，**16B**=被踢会话 id |
| `UpdateLastActive` | 更新活跃时间 |
| `GetDeviceSessions` | 列举设备会话（编码见 **`01-JNI.md`**） |
| `CleanupExpiredDeviceSessions` | 清理过期设备会话 |

实现 MUST：首参 **`callerSessionId`**，须通过与会话 **`VerifySession` 等价**的校验（**`JniBridge`** 使用 **`TryGetSessionUserId`**，见 **`docs/06-Appendix/01-JNI.md`** 文首与 **`JniSecurityPolicy.h`**）。

---

## 七、相关文档

| 文档 | 用途 |
|------|------|
| [04-Session.md](../03-Business/04-Session.md) | IM 会话 idle、心跳 |
| [01-JNI.md](../06-Appendix/01-JNI.md) | 完整 JNI 表 |
| [05-ZChatIM-Implementation-Status.md](../02-Core/05-ZChatIM-Implementation-Status.md) | MM1/JNI 实现进度 |
| [README.md](../README.md) | MM1 内存 vs MM2 磁盘 |
