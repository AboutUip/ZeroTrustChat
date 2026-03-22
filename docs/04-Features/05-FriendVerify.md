# 好友验证技术规范

> **冲突处理**：**`MM2::StoreFriendRequest` / `UpdateFriendRequestStatus` / `DeleteFriendRequest` / `CleanupExpiredFriendRequests` 已落 SQLite 表 `friend_requests`**（元库 **`user_version=5`**，见 **`03-Storage.md` 第2.6节**、**`05` 第2.1节**）。**签名校验**仍须在 **MM1 / JNI** 路径完成（**`01-JNI.md`**）；**`MM2` 不验证 Ed25519**。下文含 **产品与协议目标**。

## 一、请求流程

```
用户A发送好友请求:
1. 构造 FRIEND_REQUEST
2. 使用 Identity Key 签名
3. 加密 → 存 MM2
4. 推送至用户B
5. 用户B验证签名 → 确认身份
6. 用户B收到 → 显示待确认

用户B响应:
1. 同意/拒绝 FRIEND_RESPONSE (带签名)
2. 双方验证签名
3. 双方更新好友列表 (.zdb)
4. 删除请求记录 (MM2)
```

## 二、请求结构

```
┌─────────────────────────────────────────────────────────────┐
│  好友请求 (MM2)                                              │
├─────────────────────────────────────────────────────────────┤
│  requestId: 请求ID                                           │
│  fromUserId: 发送者                                          │
│  toUserId: 接收者                                            │
│  timestamp: 请求时间                                         │
│  signature: Ed25519 签名                                     │
│  status: pending/accepted/rejected                          │
└─────────────────────────────────────────────────────────────┘
```

## 三、签名验证

```
发送方:
1. 使用 Identity Key 对请求内容签名
2. 签名包含: fromUserId + toUserId + timestamp

接收方:
1. 从 MM1 获取发送者 Identity Key
2. 验证签名
3. 签名无效 → 拒绝请求
```

## 四、双向删除 (标记删除)

```
用户A删除用户B:
1. 发送 DELETE_FRIEND (带签名)
2. MM1 标记双方关系为"已删除"
3. 双方好友列表更新为"已删除"状态
4. 再次添加: 识别为"已删除"可恢复

状态:
- 正常: 好友关系存在
- 已删除: 标记删除，可恢复
```

## 五、已注销用户处理

```
用户A查看好友B:
1. 查询B的用户状态
2. B已注销 → 显示"已注销账号"
3. 好友关系: 标记为"对方已注销"
4. 无法发送消息给对方

显示逻辑:
- 正常好友: 显示昵称/头像
- 已删除好友: 显示"已删除"
- 已注销好友: 显示"已注销账号"

安全:
- 不暴露注销时间
- 不暴露注销原因
- 物理获取.zdb: 无法解析
```

## 六、过期机制

| 项目 | **目标（落地后）** | **当前 C++** |
|------|-------------------|--------------|
| 存储位置 | MM2 **SQLite** **`friend_requests`** | **已实现**（**`MM2` API**） |
| 过期时间 | 7 天（产品） | **`CleanupExpiredFriendRequests` / `CleanupExpiredData`**：当前策略 **pending 超 30 天**（秒级），可调（**`05` 第2.2节**） |
| 服务重启 | 持久化后 **不丢** | **库在则记录在**（除非删库 / 清理 API） |

**禁止**：写「好友请求仅存 **MM2 内存**、重启必丢」——与 **目标落库** 及 **文件分片已落盘** 的模块边界混淆。

## 七、状态流转

```
pending → accepted (同意)
pending → rejected (拒绝)
pending → expired (7天后)

正常 → 已删除 (删除)
已删除 → 正常 (恢复/重新添加)
正常 → 已注销 (对方注销)
```

## 八、安全保证

- 签名验证: Ed25519
- 请求加密: AES-256-GCM
- 双向删除: 标记同步
- 列表存储: .zdb 加密
- 注销状态: 不泄露

---

## 附录 A：MM1 Ed25519 canonical v1（与 `FriendVerificationManager.cpp` 一致）

**公钥来源**：**`UserData`** 类型 **`0x45444A31`**（32B），与 **Recall / MessageReply** 路径一致。

| 操作 | ASCII 前缀（无 `\0`） | 载荷拼接（字节序） |
|------|----------------------|-------------------|
| 发请求 | **`ZChatIM\|SendFriendRequest\|v1`** | **`fromUserId`(16) ‖ `toUserId`(16) ‖ `timestampSeconds`(uint64 BE)** |
| 响应 | **`ZChatIM\|RespondFriendRequest\|v1`** | **`requestId`(16) ‖ `accept`(1：0/1) ‖ `responderId`(16) ‖ `timestampSeconds`(uint64 BE)** |
| 删好友 | **`ZChatIM\|DeleteFriend\|v1`** | **`userId`(16) ‖ `friendId`(16) ‖ `timestampSeconds`(uint64 BE)**（**`userId`** 为发起删除方，须与 **JNI principal** 一致） |

**`GetFriends`**：由 **`friend_requests`** 中 **`status=1`（accepted）** 的 **`from_user`/`to_user`** 边推导对端 **user_id**（**无单独 friends 表**）。**`DeleteFriend`**：验签成功后 **DELETE** 上述 **accepted** 边（两端无向匹配）。

---

## 九、好友备注

```
存储:
- 备注名: 用户自定义好友昵称
- 存储位置: .zdb (加密)
- 仅好友双方可见

修改:
- 发送 UPDATE_FRIEND_NOTE (带签名)
- MM1 验证身份后更新
- 对方无需确认
```

## 十、个人资料

```
资料内容:
- 昵称: 用户昵称
- 头像: 头像加密存储
- 签名: 个性签名

访问控制:
- 好友: 可查看基本资料
- 非好友: 不可查看
- 已注销: 显示"已注销账号"

存储:
- .zdb 加密存储
- 头像: .zdb 文件
```
