# ZChatIM JNI 路由说明

与 [`docs/06-Appendix/01-JNI.md`](../../docs/06-Appendix/01-JNI.md) 分工：**01-JNI** 为方法签名、类型与 **principal 绑定矩阵**；本文为信任边界、路由落点与禁止项。修改契约须同步：`01-JNI.md`、本文、`JniInterface.h`、`JniBridge.h`。构建见 [`Build.md`](Build.md)。

## 0. 边界与不变量

1. **撤回/删除**：仅经 `mm1::MessageRecallManager`；禁止 JniBridge 直调 `mm2::MM2::DeleteMessage`。
2. **Mention**：须 `MentionPermissionManager::ValidateMentionRequest`（含签名校验）通过后再 `RecordMentionAtAllUsage`。
3. **好友**：经 `FriendManager` / `FriendVerificationManager` 验签与状态机；禁止绕过到 MM2。
4. **回复关系**：须 `MessageReplyManager::StoreMessageReplyRelation` 验签后再 `MM2::StoreMessageReplyRelation`。
5. **存储完整性**：`.zdb` 与 SQLite 须经 `StorageIntegrityManager` / 块哈希记录与比对；失败须拒绝或标记，不得静默继续。
6. **callerSessionId**：除 `Initialize` / `InitializeWithPassphrase` / `Cleanup` / `Auth` / `VerifySession` / 本地账户五接口 / `ValidateJniCall`（两重载）外，业务首参均为 caller（16B，同 `Auth` 返回值）。`TryBindCaller`；已 `Initialize` 时与 `VerifySession(caller)` 对同一会话等价。principal 与入参 id 的绑定以 **01-JNI 绑定矩阵** 与 `JniBridge.cpp` 为准。`DestroySession`：两会话须解析为同一 principal。细则 `JniSecurityPolicy.h`。
7. **并发**：JNI 业务段在 `JniBridge::m_apiRecursiveMutex` 下串行。`m_initialized` 与 `Initialize`/`Cleanup`/`EmergencyWipe` 的语义见 `JniSecurityPolicy.h` 第5节。`MM1::Get*Manager()` 仅保证取引用瞬间持锁；`MessageQueryManager` 仅在 MM2 已持 `m_stateMutex` 时使用。同时触及 MM1 与 MM2 时锁顺序见 `JniSecurityPolicy.h` 第7条。

## 0.5 常见 IM 能力与 JNI（产品）

| 能力 | JNI（camelCase，与 `ZChatIMNative` 一致） | 说明 |
|------|------------------------------------------|------|
| 冷启动 / 解锁库 | `initialize` 或 `initializeWithPassphrase` → `getStatus` | 先于业务；口令见下文第2节（生命周期） |
| 登录 | `auth` → 保存 sessionId 作 caller | 凭证由上层/服务端签发 |
| 会话有效 | `verifySession` | 与第0节第6条对齐 |
| 登出本会话 | `destroySession` | 同人双会话互毁 |
| 对称释放 | `cleanup` | 与 `initialize` 成对 |
| 发消息 | `storeMessage` | 返回 16B messageId；principal→RAM sender |
| 按 id 取 | `retrieveMessage` | |
| 会话最近消息 | `getSessionMessages` | 不强制 imSessionId==principal |
| 按用户最近 N 条 | `listMessages` | userId 须与 principal 一致；行编码见 `MessageQueryManager.h` |
| 增量拉取 | `listMessagesSinceTimestamp` / `listMessagesSinceMessageId` | 编码同 01-JNI 第二节 |
| 已读 / 未读 | `markMessageRead` / `getUnreadSessionMessageIds` | |
| 回复链 | `storeMessageReplyRelation` → `getMessageReplyRelation` | 须 MM1 验签链路（第0节第4条） |
| 编辑 | `editMessage` → `getMessageEditState` | 规则见 [`09-MessageEdit.md`](../../docs/04-Features/09-MessageEdit.md) |
| 删除/撤回 | `deleteMessage` / `recallMessage` | `MessageRecallManager` |
| 会话心跳/清理 | `touchSession`、`getSessionStatus`、`cleanupExpiredSessions`、`cleanupSessionMessages` | IM 活跃表见 [`04-Session.md`](../../docs/03-Business/04-Session.md) |
| 文件分片 | `storeFileChunk` … `completeFile` / `cancelFile` | 续传：`storeTransferResumeChunkIndex` 等；见 [`08-FileTransfer.md`](../../docs/04-Features/08-FileTransfer.md)、Storage 第七节 |
| 好友 | `sendFriendRequest`、`respondFriendRequest`、`getFriends`、`deleteFriend` | canonical 见 [`05-FriendVerify.md`](../../docs/04-Features/05-FriendVerify.md) |
| 群 | `createGroup`、`inviteMember`、`removeMember`、`leaveGroup`、`getGroupMembers`、`updateGroupKey` | 角色见 `GroupManager` |
| @ / 禁言 / 群名 | `validateMentionRequest`、`recordMentionAtAllUsage`；`muteMember`/`isMuted`/`unmuteMember`；`updateGroupName`/`getGroupName` | [`12-Mention.md`](../../docs/04-Features/12-Mention.md)、[`11-GroupMute.md`](../../docs/04-Features/11-GroupMute.md)、[`13-GroupName.md`](../../docs/04-Features/13-GroupName.md) |
| 多设备 | `registerDeviceSession` 等 | Java 返回语义见 01-JNI 第八节；[`06-MultiDevice.md`](../../docs/04-Features/06-MultiDevice.md) |
| 在线缓存 | `getUserStatus` | 最后已知；服务端权威 |
| UserData | `storeUserData` / `getUserData` / `deleteUserData` | 编辑用公钥 type=`0x45444A31` |
| 好友备注 | `updateFriendNote` | [`05-FriendVerify.md`](../../docs/04-Features/05-FriendVerify.md) 第九节 |
| 本地销户标记 | `deleteAccount` / `isAccountDeleted` | 非全库擦除；[`06-AccountDelete.md`](../../docs/03-Business/06-AccountDelete.md) |
| 主密钥/会话密钥 | `generateMasterKey`、`refreshSessionKey` | 禁止 JNI 失锁直调 `GetKeyManagement()` |
| 主密钥轮换 | `rotateKeys` | `MM1::RefreshMasterKey`（锁内） |
| SPKI Pin / 封禁 | `configurePinnedPublicKeyHashes`、`verifyPinnedServerCertificate` 等 | ZSP 承载 TLS 见 [`02-ZSP-Protocol.md`](../../docs/01-Architecture/02-ZSP-Protocol.md) 第十节；caller 可空见 `JniSecurityPolicy.h` |
| JNI 自检 | `validateJniCall` | 强校验优先；非唯一安全边界 |
| 运维 | `cleanupExpiredData`、`optimizeStorage`、`emergencyWipe` 等 | |

## 0.6 架构不提供或无专门 JNI 的能力

**须 App/服务端补齐（无对应 JNI 或仅为 C++ 内部）**：用户注册/开户/忘记密码主流程走 **ZSP + 网关**，客户端用 `auth`；本地口令用 `registerLocalUser`…`resetLocalPasswordWithRecovery`（01-JNI 一.1）。推送/WebSocket/FCM 无 JNI。历史漫游的**同步策略与游标**在 App。批量写消息若仅存在 `MM2::StoreMessages` 而无 JNI，则须循环 `storeMessage` 或扩 JNI。**实时音视频媒体面**不在库内；**B5**：`Rtc*` 仅状态机 + callId，信令 **ZSP `CALL_SIGNAL`**（`02-ZSP-Protocol` 第6.6节）。全文搜索等以 `MessageQueryManager` 为界。

**常见 UI 能力无 1:1 JNI**：草稿、typing、会话列表元数据、删整会话、转发、红包等——App 状态、`storeUserData` 自定义 type 或自建 DB。

**阅读顺序**：01-JNI（签名/principal）→ 本文第0节 → 第2节分组路由 → Storage/Feature 文档。

## 1. 返回语义

1. `bool`：true 成功；false 失败或校验未过。  
2. `vector<uint8_t>`：空表示 null/失败（另有说明除外）。  
3. `vector<vector<uint8_t>>`：空表示无条目，不一定等价于失败。  
4. 未写 caller 的 API 小节仍以首参 caller 为准（与 01-JNI 一致）。`imSessionId` 为 16B IM 通道，非 ZSP 头 4 字节。

## 2. 分组路由（入参/返回编码以 01-JNI 为准）

| 节 | 路由要点 |
|----|----------|
| **2.0 生命周期** | `MM1::Initialize` + `MM2::Initialize`；幂等与路径见 `JniBridge::Initialize`；`cleanup`：`MM2::Cleanup` → 清 Auth → `MM1::Cleanup` → `Notify*`（`JniSecurityPolicy.h` 第8节） |
| **2.1 认证** | `AuthSessionManager`；token/IP 限流见 [`02-Auth.md`](../../docs/03-Business/02-Auth.md) |
| **2.2 消息** | `storeMessage`→`MM2::StoreMessage`（RAM）；`list*`→`GetMessageQueryManager`；`delete`/`recall`→`MessageRecallManager`（禁止直调 `MM2::DeleteMessage`）；`storeMessageReplyRelation`→`MessageReplyManager` 再 MM2；`editMessage`→`MessageEditOrchestration`→`ApplyEdit` |
| **2.3 用户数据** | `UserDataManager`→`MM2` `mm1_user_kv`（须 MM2 已 Initialize） |
| **2.4 好友** | `FriendManager` / `FriendVerificationManager`→`MM2` `friend_requests` |
| **2.5 群与 Mention/Mute/群名** | `GroupManager` / `GroupMuteManager` / `GroupNameManager` / `MentionPermissionManager`→`MM2` 与元表；principal 与 role 见 01-JNI 与 `GroupManager` |
| **2.6 文件** | `MM2::StoreFileChunk` / `GetFileChunk` / `CompleteFile` 等；块哈希与 `StorageIntegrityManager` 见 Storage 第七节；续传索引仅进程内 |
| **2.7 会话与多设备** | `SessionActivityManager` / `DeviceSessionManager` / `UserStatusManager`→`MM2` 元表（`user_version=11`）；须 `initialize` 成功 |
| **2.8 清理与统计** | `MM2` 运维 API；返回粒度由实现决定，不泄露明文 |
| **2.9 密钥/Pin/销户/备注** | `generateMasterKey`/`refreshSessionKey`/`rotateKeys`/`emergencyWipe`：`MM1` 锁内路径；`emergencyWipe` 与 `Notify*` 见 `01-MM1.md`、`JniSecurityPolicy.h` 第8节。Pin：`CertPinningManager`。销户/备注：`AccountDeleteManager` / `FriendNoteManager` |
| **2.10 验证** | `validateJniCall(env,class)` 优先；`JniSecurity::AllocateJniMemory` 等见 [`Scope.md`](Scope.md) M0 |

**典型禁止**：JniBridge 绕过 MM1 验签直调 MM2 的删除/回复/好友/Mention；将 `SeedAcceptedFriendshipForSelfTest` 等自测符号接 JNI（见 `JniSecurityPolicy.h`）。

## 3. 实现闭环（摘要）

1. **路由**：持桥接锁 → caller → MM1 校验 → MM2。  
2. **完整性**：`StoreFileChunk`/`GetFileChunk` 路径上 record/verify 哈希（`03-Storage.md` 第七节）。  
3. **事务**：`.zdb` 与 SQLite 无单事务；部分失败与回滚见 [`Implementation-Status.md`](Implementation-Status.md) 第8节。  
4. **契约**：改 `.h` 必同步 01-JNI 与本文。

## 4. 验收摘要

安全敏感路径可观测进入 MM1 manager；文件路径有哈希闭环；签名错误/会话失效/频率限制等按设计拒绝。正式门槛：`ZChatIM --test`、Implementation-Status 第7节。
