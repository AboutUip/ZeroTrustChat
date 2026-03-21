# @提及技术规范

> **文档类型**：**ZSP 载荷 TLV** + **MM1 权限校验**。  
> **编号注意**：**TLV Type `0x15`（Mention）** 与 ZSP **消息类型**表中的 **`0x15` = GROUP_REMOVE** 是**不同命名空间**（见 **`02-ZSP-Protocol.md` 第7.2节 文首说明**）。本文 **0x15** 均指 **TLV 扩展**。  
> **JNI**：**`ValidateMentionRequest`**、**`RecordMentionAtAllUsage`**（**`01-JNI.md`** 第六节）。  
> **实现状态**：**`05-ZChatIM-Implementation-Status.md` 第3节** — MM1 **`MentionPermissionManager`** 等 **无完整 `.cpp` / 桥未接**；**禁止**在 JniBridge 绕过签名校验直调 Record。

---

## 一、功能

```
群聊中@提及:
- @个人: 提醒特定成员
- @ALL: 提醒全体成员
```

---

## 二、TLV扩展

```
使用 ZSP TLV 扩展（第七节）:

Type: 0x15 (Mention)
Length: 可变
Value:
┌─────────────────────────────────────┐
│  Type: 1=个人 2=ALL                │
│  Count: 提及人数                    │
│  UserIDs[]: 被提及用户ID列表       │
└─────────────────────────────────────┘
```

---

## 三、消息结构

```
TEXT + TLV 0x15:
┌─────────────┬──────────────────┐
│ Content     │ TLV 0x15         │
│ (包含@xxx) │ Mention数据       │
└─────────────┴──────────────────┘
```

---

## 四、处理流程

```
发送方:
1. 消息内容包含@xxx格式
2. 解析@后用户名
3. 获取用户ID
4. 构建TLV 0x15扩展
5. 加密发送

接收方:
1. 解密消息
2. 解析TLV 0x15
3. 提取被提及用户
4. 发送强提醒通知
5. 显示@标记
```

---

## 五、@ALL

```
@ALL 规则:
- 仅群主/管理员可使用
- 发送时检查权限
- 全体成员收到强提醒

限制:
- 每分钟最多3次@ALL
- 防止滥用
```

---

## 六、安全

```
验证:
- 检查发送者是否有@权限
- 验证被提及用户是否为成员
- 签名验证发送者身份（Ed25519，见 JNI `signatureEd25519`）
```

**路由（JNI-API-Documentation）**：**`validateMentionRequest`** MUST 走 **`mm1::MentionPermissionManager::ValidateMentionRequest`**；**仅通过后**方可 **`recordMentionAtAllUsage`**（@ALL 计数）。

---

## 七、与 MM2

**`@提及` 元数据**默认 **不落 `im_messages`**（该表仅 `message_id`/`session_id`）；持久化策略（是否单独表）由 **MM1/SQLite 扩展** 决定，**非**当前 **`03-Storage.md`** 第二节 所列核心表。

---

## 八、相关文档

| 文档 | 用途 |
|------|------|
| [02-ZSP-Protocol.md](../01-Architecture/02-ZSP-Protocol.md) | TLV 0x15、与 MessageType 区分 |
| [01-JNI.md](../06-Appendix/01-JNI.md) | `ValidateMentionRequest` 等 |
| [ZChatIM/docs/JNI-API-Documentation.md](../../ZChatIM/docs/JNI-API-Documentation.md) | 职责分离不变量 |
| [05-ZChatIM-Implementation-Status.md](../02-Core/05-ZChatIM-Implementation-Status.md) | 实现进度 |
