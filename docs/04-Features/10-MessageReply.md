# 消息回复技术规范

> **实现状态**：**`MM2::StoreMessageReplyRelation` / `GetMessageReplyRelation`** 落在 **MM2 进程内 RAM**（**`m_imRamReplies`**）。**元数据库**不含 **`im_message_reply` 表**；**`PRAGMA user_version=11`**。**`StoreMessageReplyRelation`** 另：**同会话**、**`repliedSenderId` 与父消息 RAM `senderUserId` 一致**；若 **`imSessionId`** 在 **`group_members`** 有行（群会话），则 **SQL** 校验 **回复作者** 与 **`repliedSenderId`** 均为该 **`group_id`** 成员。**`mm1::MessageReplyManager`**：**Ed25519** + **MM2**。**JNI**：**`storeMessageReplyRelation` / `getMessageReplyRelation`** 已接 **`JniBridge`**。下文为 **产品与 ZSP 目标**。  
> **TLV**：**`Type = 0x10` `MessageReply`** 为 **载荷内 TLV**（**`02-ZSP-Protocol.md` 第7.2节**），**≠** **MessageType `0x10` `GROUP_UPDATE`**（第五节）。

## 一、回复流程

```
用户A回复用户B的消息:
1. 用户A选择要回复的消息
2. 构造回复消息 (携带被回复消息ID)
3. 加密发送
4. 服务器路由
5. 用户B收到，显示回复内容
```

## 二、TLV扩展

```
回复使用 ZSP TLV 扩展:

Type: 0x10 (MessageReply)
Length: 可变
Value:
┌─────────────────────────────────────┐
│  repliedMsgId: 被回复消息ID           │
│  repliedContent: 被回复消息摘要       │
│  repliedSender: 被回复发送者ID        │
└─────────────────────────────────────┘
```

## 三、显示效果

```
接收方显示:
┌─────────────────────────────────────┐
│  ← 回复 @用户B                       │
│  [被回复消息内容摘要...]               │
├─────────────────────────────────────┤
│  [回复消息内容]                       │
└─────────────────────────────────────┘
```

## 四、安全机制

```
验证:
- 验证回复者是否为群成员/好友
- 签名验证发送者身份

限制:
- 好友/群成员才能回复
- 被删除消息仍可显示"原消息已删除"
```

## 五、消息关系

```
存储结构:
- msgId: 当前消息ID
- repliedMsgId: 被回复消息ID
- 形成消息链: msgId → repliedMsgId → ...

用途:
- 消息撤回时: 同时通知被回复的消息
- 消息删除时: 保留回复关系
```
