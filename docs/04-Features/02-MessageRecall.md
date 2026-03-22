# 消息撤回技术规范

> **与当前 `ZChatIM` C++**：JNI 契约要求 **`deleteMessage` / `recallMessage`** 经 **`mm1::MessageRecallManager`**，**禁止**直调 **`MM2::DeleteMessage`**（见 **`docs/06-Appendix/01-JNI.md`** 路由摘要）。MM2 侧 **`DeleteMessage`** 为 **`ImRamEraseUnlocked`**（**RAM IM** 删除；**不**动文件分片 **`data_blocks`**）。下文「物理删除」指 **逻辑不可恢复** 的产品语义（IM **无**磁盘密文可覆写）。

## 一、撤回流程

```
用户发送撤回请求:
1. 携带 msgId
2. MM1 校验签名与权限 → 调度 MM2 删除/覆写
3. Level 2 覆写（MM1/产品层）
4. 删除/更新 MM1 侧会话与索引状态
5. 返回成功
```

## 二、强制撤回

```
撤回规则:
- 未读消息: 可撤回
- 已读消息: 可撤回 (强制)
- 无论是否已读，都可撤回

验证:
- 仅消息发送者可撤回
- 签名验证身份
```

## 三、销毁级别

```
Level 2 覆写:
1. 覆写 0x00
2. 覆写 0xFF
3. 覆写随机数
4. 释放内存
```

## 四、接收方处理

```
收到撤回通知:
1. 本地标记"已撤回"
2. 不显示内容
3. 显示"消息已撤回"
4. 已读/未读均可撤回
```

## 五、安全保证

- MM2：**`DeleteMessage` → `ImRamEraseUnlocked`**（**RAM IM** 删除；**不**动文件分片的 **`data_blocks`**；见 **`03-Storage.md` 第七节**、**`05` 第8节**）
- 索引与块一致（失败路径见 **`ZChatIM/docs/Implementation-Status.md` 第8节**）
- Level 2 覆写（MM1 内存与策略）
- 强制撤回
