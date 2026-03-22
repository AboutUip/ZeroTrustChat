# 消息编辑技术规范

> **实现状态**：**`MM2::EditMessage` / `GetMessageEditState` 已接 RAM**（**`edit_count` / `last_edit_time_s` + 替换 `ImRamMessageRow.blob`**，见 **`03-Storage.md` 第2.6节**、**`05` 第2.2节**）。**JNI** **`editMessage` / `getMessageEditState`** 已接 **`JniBridge`**；**`MessageEditManager::ApplyEdit`** 当前多为**桩**（**未**做完整 Ed25519 产品校验），**产品级编辑闭环**仍待 MM1 实现。下文为 **产品与协议目标**。  
> **TLV**：**`Type = 0x12` `MessageEdit`**（**`02-ZSP-Protocol.md` 第7.2节**）；**≠** **MessageType `0x12` `FRIEND_REQUEST`**（第五节）。

## 一、编辑流程

```
用户编辑消息:
1. 发送 MESSAGE_EDIT (TLV 0x12)
2. 携带: msgId + 新内容
3. MM1 验证发送者身份
4. MM2 更新消息内容
5. 通知接收方消息已编辑
```

## 二、消息结构

```
┌─────────────────────────────────────────────────────────────┐
│  MESSAGE_EDIT (TLV 0x12)                                    │
├─────────────────────────────────────────────────────────────┤
│  msgId: 原消息ID                                             │
│  newContent: 新内容 (加密)                                   │
│  timestamp: 编辑时间                                         │
│  signature: Ed25519 签名                                    │
└─────────────────────────────────────────────────────────────┘
```

## 三、限制规则

```
时间限制:
- 消息发送后 5 分钟内可编辑
- 超过 5 分钟 → 拒绝编辑
- 验证: timestamp - msg.timestamp < 300s

次数限制:
- 每条消息最多编辑 3 次
- 超过 3 次 → 拒绝编辑
- 验证: editCount < 3

验证失败:
- 返回 E_EDIT_TIMEOUT (超时)
- 返回 E_EDIT_LIMIT (次数超限)
```

## 四、安全机制

```
验证:
1. 验证发送者是否为原消息发送者
2. 验证编辑时间 < 5分钟
3. 验证编辑次数 < 3次
4. 签名验证: Ed25519

限制:
- 仅支持文本消息编辑
- 仅发送方可编辑
- 最多编辑3次
- 5分钟内有效
```

## 五、接收方处理

```
收到编辑通知:
1. 更新本地消息显示
2. 显示"已编辑"标记
3. 显示编辑时间
4. 显示编辑次数
```

## 六、存储

```
MM2:
- 更新消息内容
- 记录 editCount
- 记录 lastEditTime
- 不保留编辑历史
```
