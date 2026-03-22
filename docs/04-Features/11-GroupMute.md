# 群组禁言技术规范

> **文档类型**：**产品与 MM1 权限模型**；禁言状态**不**由 MM2 **`im_messages`** 表达。  
> **JNI**：**`docs/06-Appendix/01-JNI.md`**「六、群聊安全特性」——`MuteMember`、`IsMuted`、`UnmuteMember`。  
> **实现状态**：**`05-ZChatIM-Implementation-Status.md` 第3节** — **`mm1::GroupMuteManager`**（**`GroupMuteManager.cpp`**）已实现；持久化 **`MM2::SqliteMetadataDb`** 表 **`mm2_group_mute`**（**`user_version=6`**）；**JNI** 经 **`JniBridge`** 路由至该管理器（见 **`ZChatIM/docs/JNI-API-Documentation.md`**「GroupMute」）。  
> **ZSP**：禁言信令消息类型为 **`0x14` `GROUP_MUTE`**（**`02-ZSP-Protocol.md` 第五节**）。

## 一、禁言流程

```
群主/管理员禁言成员:
1. 发送 GROUP_MUTE（ZSP MessageType **0x14**）
2. 指定成员ID和禁言时长
3. MM1 **`GroupMuteManager`** 写入 **`mm2_group_mute`**（经 **MM2**）
4. 广播通知

被禁言成员:
- 无法发送消息
- 仍可接收消息
- 仍可查看群信息
```

## 二、禁言结构

```
┌─────────────────────────────────────────────────────────────┐
│  禁言记录（MM2 **`mm2_group_mute`**；MM1 **`GroupMuteManager`**）│
├─────────────────────────────────────────────────────────────┤
│  groupId: 群ID（16B）                                         │
│  userId: 被禁言用户ID（16B）                                  │
│  mutedBy: 禁言操作者ID（16B）                                  │
│  start_ms: 开始时间（Unix 毫秒）                               │
│  duration_s: 时长秒；**-1**=永久；否则结束=start_ms+duration_s×1000 │
│  reason: 禁言原因 BLOB，可空，**≤4096 字节**                   │
└─────────────────────────────────────────────────────────────┘
```

## 三、禁言时长

| 时长 | 说明 |
|------|------|
| 60秒 | 1分钟 |
| 300秒 | 5分钟 |
| 1800秒 | 30分钟 |
| 3600秒 | 1小时 |
| 86400秒 | 24小时 |
| -1 | 永久禁言 |

## 四、解禁

```
解禁方式:
1. 手动解禁: 群主/管理员手动解除
2. 自动解禁: 禁言时长到期（**`CleanupExpiredMutes` / `MM2::CleanupExpiredData`** 删除到期行）
3. 退群: 产品层应视为禁言失效；**当前 C++ 本地库未**在退群时级联删 **`mm2_group_mute`**（可后续补强或依赖到期清理）
```

## 五、权限检查

```
发送消息时:
1. 检查用户是否在群中
2. 检查用户是否被禁言
3. 禁言中 → 返回错误码 E_GROUP_MUTED
4. 未禁言 → 正常发送
```

**与 MM2**：校验通过后客户端仍可调用 **`MM2::StoreMessage`**；**禁言须在 MM1/服务端拦截**，不可仅靠客户端。

## 六、权限级别（与当前 C++ 一致）

| 操作 | 群主 | 管理员 | 普通成员 |
|------|------|--------|----------|
| 禁言他人 | ✓（不可禁群主；不可自禁） | ✓ **仅可禁普通成员** | ✗ |
| 解禁他人 | ✓ | ✓（**不**校验「是否本人发出的禁言」） | ✗ |
| 被禁言 | - | - | - |

**时间语义**：**`duration_s = -1`** 永久；**正整数** 为限时；**`now_ms < start_ms`** 时 **`IsMuted`** 为否（尚未生效）。**到期** 可由 **`GroupMuteManager::CleanupExpiredMutes`** 或 **`MM2::CleanupExpiredData`** 清理行（后者用系统当前时间）。

## 七、通知

```
禁言时:
1. 通知被禁言成员
2. 通知群主/管理员
3. 群内其他成员不可见
```

---

## 八、JNI 摘要

| 方法 | 说明 |
|------|------|
| `MuteMember` | `caller`、`groupId`、`userId`、`mutedBy`、时间窗、`reason`；须校验角色 |
| `IsMuted` | JNI 仅 **bool**；内部可有剩余时长 **out**（见 **`01-JNI.md`** 表注） |
| `UnmuteMember` | `caller`、`unmutedBy` |

---

## 九、相关文档

| 文档 | 用途 |
|------|------|
| [02-ZSP-Protocol.md](../01-Architecture/02-ZSP-Protocol.md) | **0x14 GROUP_MUTE** |
| [01-JNI.md](../06-Appendix/01-JNI.md) | 完整 JNI 表 |
| [05-ZChatIM-Implementation-Status.md](../02-Core/05-ZChatIM-Implementation-Status.md) | MM1 进度 |
| [03-Storage.md](../02-Core/03-Storage.md) | MM2 表结构（与禁言记录分离） |
