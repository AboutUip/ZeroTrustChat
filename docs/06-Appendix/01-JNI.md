# JNI 接口清单

**严格对齐**：本表 **`C++` 列**与 **`ZChatIM/include/jni/JniInterface.h`** 中 `static` 方法名及重载一一对应；**`JniBridge.h`** 实例方法同名、同参数、同返回值（无 `static`）。**`逻辑（camelCase）` 列**供 Java/JNI 命名参考，与 C++ `PascalCase` 为同一契约。

**产品视角（常见 IM 操作 → 调哪些 JNI；边界见 §0.5.1～§0.5.2）**：**`ZChatIM/docs/JNI-API-Documentation.md` §0.5～§0.5.3**。**注册/忘记密码（B1）** 与 **语音视频通话（B5）** 的衔接说明见该文档 **§0.5.1.1、§0.5.1.2**。本文件仍为**逐 API 契约与 principal 矩阵**的权威表。

**维护规则**：增删改任一 JNI 入口时，须**同步**更新本文件、`ZChatIM/docs/JNI-API-Documentation.md`、**`ZChatIM/include/jni/JniInterface.h`**、**`ZChatIM/include/jni/JniBridge.h`**。

**类型简写**（与头文件一致）：`bytes` = `std::vector<uint8_t>`；`bytes[]` = `std::vector<std::vector<uint8_t>>`；`mapSS` = `std::map<std::string,std::string>`。

**callerSessionId（最高安全性默认）**：
- 缩写 **`caller`** 表示首参 `callerSessionId`（`bytes`，长度 **`Types::JNI_AUTH_SESSION_TOKEN_BYTES`**，与 `Auth` 返回句柄一致）。
- **不含** `caller` 的 API：`Initialize` / **`InitializeWithPassphrase`** / `Cleanup` / `Auth` / `VerifySession` / **`RegisterLocalUser`** / **`AuthWithLocalPassword`** / **`HasLocalPassword`** / **`ResetLocalPasswordWithRecovery`** / `ValidateJniCall` 两重载。
- **其余全部**业务 API 首参均为 `caller`；**`JniBridge`** 用 **`TryBindCaller` → `AuthSessionManager::TryGetSessionUserId`** 判定会话有效。**在桥接已 `Initialize`（**`JniBridge::m_initialized`** 为 **`std::atomic<bool>`**，语义等价于「已成功 `Initialize`」）时**，与 **`VerifySession(caller)`** 对**同一**会话 id **结论等价**；未初始化时二者均以失败/空路径短路。**principal 与后续 id 的绑定并非一律**：见下文 **「principal 绑定矩阵」**（与 **`src/jni/JniBridge.cpp`** 严格一致；策略头文件 **`JniSecurityPolicy.h`** 为摘要）。
- **`imSessionId`**：即时通讯**会话通道** ID（`StoreMessage` / `GetSessionMessages` / `TouchSession` 等），与 ZSP 头内 4 字节 `SESSION_ID_SIZE` **无关**。
- **`DestroySession`**：`caller` + `sessionIdToDestroy`；**当前 `JniBridge`**：两会话均须 `TryBindCaller` 成功且解析出的 **`principal` 相同**（同人双会话互毁）；**未**实现「运维代销毁他人会话」等扩展授权（若产品需要须另 API 或 RBAC）。
- **TLS 回调**：`VerifyPinnedServerCertificate` / `RecordFailure` 等仍带 `caller`；`caller` **可为空** 仅当实现可证明等价来源（见策略头文件）。

**并发（头文件契约）**：**`JniBridge::m_apiRecursiveMutex`**（整段 JNI 业务调用）、**`MM1::m_apiRecursiveMutex`**、**`MM2::m_stateMutex`**（递归）；**`JniBridge::m_initialized`**：**`std::atomic<bool>`**，与 **`Initialize`/`Cleanup`/`EmergencyWipe`** 的 **release/acquire** 语义见 **`include/common/JniSecurityPolicy.h` §5**。**`MM1::Get*Manager()`** 仅在**取引用**时短暂持 MM1 锁，**链式调用**后续 Manager 方法时还须依赖桥接锁 / Manager 自锁 / MM2 锁（**细目**：**`ZChatIM/docs/JNI-API-Documentation.md` §0 第7条**）。**`MessageQueryManager`** 仅在 MM2 已持 **`m_stateMutex`** 时使用。

**`Auth` 第三参 `clientIp`**：可为空；非空时 `AuthSessionManager` 启用 IP 级限流与 `userId‖IP` 封禁键（见 `docs/03-Business/02-Auth.md` 第七节）。

**路由摘要**（与 `JNI-API-Documentation.md` 一致）：
- `storeMessage` → **`MM2::StoreMessage(imSessionId, principal, payload)`**（**RAM `senderUserId`**，与 **RAM 行 / 历史文档列** **`sender_user_id`** 语义一致，供 **编辑/撤回** 与 **`senderId`** 绑定；**`imSessionId` 不必**等于 **`principal`**）
- `listMessages*` → `mm2::MM2::GetMessageQueryManager()`
- `createGroup` / `inviteMember` / `removeMember` / `leaveGroup` / `getGroupMembers` / `updateGroupKey` → **`mm1::GroupManager`**（经 **`MM2::*ForMm1`** 写 **`group_members` / `mm2_group_display` / `group_data`+`.zdb`（**`ZGK1`**）**）
- `muteMember` / `isMuted` / `unmuteMember` → **`mm1::GroupMuteManager`** → **`MM2` / `mm2_group_mute`**（**`removeMember`/`leaveGroup`** 经 **`DeleteGroupMemberForMm1`** 会先删对应禁言行，见 **`03-Storage.md` 第2.6节**）
- `updateGroupName` → `mm1::GroupNameManager` 校验后 → `mm2::MM2::UpdateGroupName`
- `getGroupName` → `mm2::MM2::GetGroupName`
- `deleteMessage` / `recallMessage` → **`mm1::MessageRecallManager`**（**`sender_user_id` 绑定** + **Recall canonical 验签**；禁止直调 **`MM2::DeleteMessage`**）
- `editMessage` → **`mm1::MessageEditOrchestration::EditMessage`** → **`MessageEditManager::ApplyEdit`**（**Ed25519** + **`MM2::EditMessage`**）
- `validateMentionRequest` / `recordMentionAtAllUsage` → **`mm1::MentionPermissionManager`**（**@ALL** 滑动窗在 **`mm1_mention_atall_window`**，**`user_version=11`**；须 **`MM2::Initialize`**）
- `updateFriendNote` → **`mm1::FriendNoteManager`**（**`mm1_user_kv`** **`type=0x464E424E` 'FNBN'**）
- `deleteAccount` / `isAccountDeleted` → **`mm1::AccountDeleteManager`**（**墓碑 `type=0x41434431` 'ACD1'**；**不**自动清库）
- `refreshSessionKey` → **`MM1::RefreshSessionKey()`**（`MM1` 递归锁内委托 `KeyManagement`，**禁止** `GetKeyManagement()` 返回后再调，避免失锁竞态）
- `getTransferResumeChunkIndex`：C++ 失败返回 **`UINT32_MAX`**；**JNI** 转为 **`jint`** 时 Java 侧为 **-1**（与 **`ZChatIMNative.getTransferResumeChunkIndex` 注释**一致）；成功值为非负 **`int`**（chunk 索引须落在 Java `int` 可表示范围内）。

**ID 长度**：`Types.h` 中 `SESSION_ID_SIZE == 4` 表示 **ZSP 协议头**内 SessionID 字段（见 `docs/01-Architecture/02-ZSP-Protocol.md`）。**`RegisterDeviceSession` 等 JNI 参数**中 `deviceId` / `sessionId` 的 `vector<uint8_t>` 约定为 **16 字节**，与 **`USER_ID_SIZE` / `MESSAGE_ID_SIZE`** 同级二进制载荷语义一致，**不等于** ZSP 头 4 字节字段。

### principal 绑定矩阵（与 `src/jni/JniBridge.cpp` 严格一致）

**说明**：**「仅 caller」** = 桥接层 **`TryBindCaller` 成功**（及 **`Initialize` / 长度等**）即可，**不**再要求 **`imSessionId == principal`**、**不**校验「消息是否属主」。**「+ principal 绑定」** = 在 caller 有效基础上，**`principal` 须与表中指定入参 16B id** **`ConstantTimeCompare` 相等**（或 **`DestroySession`** 双会话解析出的两个 principal 互等）。

**与「仅 caller」并存的重要语义**：**`InviteMember` / `RemoveMember` / `GetGroupMembers` / `UpdateGroupKey`** 在 **JniBridge** 中**不**调用 **`PrincipalMatches(principal, userId)`**（参数里的 **`userId`** 为**被邀请/被踢**对象，本就不应与操作者相等）。但 **`mm1::GroupManager`** 将 **`TryBindCaller` 解析出的 `principal`** 作为 **邀请者 / 踢人执行者 / 成员列表查看者 / 更新群密钥发起者**，并在 SQLite 侧校验群内 **role**（邀请/踢人须 **admin/owner** 或 **owner** 等，见 **`include/mm1/managers/GroupManager.h`**）。因此：仍归在「仅 caller」行是指 **JNI 层无「principal 须等于某入参 id」** 的额外一行比对；**非**「不使用 principal」。

| 规则 | C++ API（`JniInterface` / `JniBridge` 同名） |
|------|---------------------------------------------|
| 双会话同一 principal | `DestroySession` |
| caller 有效 + **`principal` 等于参数 id**（列名见上表「方法」） | `ListMessages` / `ListMessagesSinceTimestamp` / `ListMessagesSinceMessageId`（**`userId`**）；`DeleteMessage` / `RecallMessage`（**`senderId`**）；`StoreUserData` / `GetUserData` / `DeleteUserData`（**`userId`**）；`SendFriendRequest`（**`fromUserId`**）；`RespondFriendRequest`（**`responderId`**）；`DeleteFriend` / `GetFriends`（**`userId`**）；`CreateGroup`（**`creatorId`**）；`LeaveGroup`（**`userId`**）；`ValidateMentionRequest` / `RecordMentionAtAllUsage`（**`senderId`**）；`MuteMember`（**`mutedBy`**）；`UnmuteMember`（**`unmutedBy`**）；`UpdateGroupName`（**`updaterId`**）；`RegisterDeviceSession` / `UpdateLastActive` / `GetDeviceSessions`（**`userId`**）；`GetUserStatus`（**`userId`**）；`DeleteAccount` / `IsAccountDeleted`（**`userId`**）；`UpdateFriendNote`（**`userId`**）；`EditMessage`（**`senderId`**）；**`ChangeLocalPassword`（`userId`）** |
| **仅 caller**（不比对 principal 与 `imSessionId` / 消息 id） | `StoreMessage`, `RetrieveMessage`, `MarkMessageRead`, `GetUnreadSessionMessageIds`, `GetSessionMessages`, `GetSessionStatus`, `TouchSession`, `CleanupExpiredSessions`, `CleanupSessionMessages`, `InviteMember`, `RemoveMember`, `GetGroupMembers`, `UpdateGroupKey`, `IsMuted`, `GetGroupName`, `StoreFileChunk`, `GetFileChunk`, `CompleteFile`, `CancelFile`, `StoreTransferResumeChunkIndex`, `GetTransferResumeChunkIndex`, `CleanupTransferResumeChunkIndex`, `GetMessageReplyRelation`, `GetMessageEditState`, `CleanupExpiredDeviceSessions`, `CleanupExpiredData`, `OptimizeStorage`, `GetStorageStatus`, `GetMessageCount`, `GetFileCount`, `GenerateMasterKey`, `RefreshSessionKey`, `EmergencyWipe`, `GetStatus`, `RotateKeys`, `ConfigurePinnedPublicKeyHashes`, `IsClientBanned`, `ClearBan`, **`RtcStartCall`**, **`RtcAcceptCall`**, **`RtcRejectCall`**, **`RtcEndCall`**, **`RtcGetCallState`**, **`RtcGetCallKind`** |
| **`caller` 可空**：空则跳过 `TryBindCaller`；非空则须有效 | `VerifyPinnedServerCertificate`, `RecordFailure` |
| 桥接层**不**调用 `TryBindCaller`；**`MessageReplyManager`** 内 **`TryGetSessionUserId` + `senderId`** | `StoreMessageReplyRelation` |

---

## 零、生命周期

| C++ | 逻辑（camelCase） | 输入 | 输出（C++） | 说明 |
|-----|-------------------|------|-------------|------|
| `Initialize` | initialize | dataDir(string), indexDir(string) | `bool` | 初始化可信区与桥接；路由 **`MM1::Initialize`** + **`MM2::Initialize(dataDir, indexDir)`**（路径须非空） |
| `InitializeWithPassphrase` | initializeWithPassphrase | dataDir(string), indexDir(string), messageKeyPassphraseUtf8(`const char*`，**可 null**；非 null 时须非空 C 串) | `bool` | 路由 **`MM2::Initialize(dataDir, indexDir, passphrase)`**；**`ZMKP` / 口令解锁** 等（须 **`ZCHATIM_USE_SQLCIPHER=ON`**）。**`nullptr`** 与 **`Initialize`** 等价。Java：`passphrase` 为 **`null`** 时 native 传 **`nullptr`**。 |
| `Cleanup` | cleanup | - | `void` | 释放资源 |

## 一、认证模块

| C++ | 逻辑（camelCase） | 输入 | 输出（C++） | 说明 |
|-----|-------------------|------|-------------|------|
| `Auth` | auth | userId, token, clientIp(bytes, **可空**) | `bytes`（空=null） | sessionId；**须**在 **`Initialize` / `InitializeWithPassphrase` 已成功**之后调用，否则 **`JniBridge`** 直接返回空（**`02-Auth.md` 第7.3节**）。**`userId`** 须 **16B**（**`USER_ID_SIZE`**）。**`token`**：不透明凭证须 **`≥ Types::AUTH_OPAQUE_CREDENTIAL_MIN_BYTES`（32）**、**非全零**、**不得与 `userId` 等长且逐字节相同**（**`AuthSessionManager::VerifyCredential`**）。`clientIp` 建议传 IPv4 4B/IPv6 16B 以启用 02-Auth IP 限流 |
| `VerifySession` | verifySession | sessionId | `bool` | active=true |
| `DestroySession` | destroySession | caller, sessionIdToDestroy | `bool` | 见文首 caller 策略 |

### 一.1 本地账户（**mm1_user_kv** **LPH1/LRC1**）

| C++ | 逻辑（camelCase） | 输入 | 输出（C++） | 说明 |
|-----|-------------------|------|-------------|------|
| `RegisterLocalUser` | registerLocalUser | userId, passwordUtf8, recoverySecretUtf8 | `bool` | **无 caller**；须 **`Initialize` 成功** 且 **MM2 已打开**；口令 UTF-8 长度 **`LOCAL_ACCOUNT_PASSWORD_MIN_UTF8_BYTES`～`MAX`**；恢复密钥须 **`≥ AUTH_OPAQUE_CREDENTIAL_MIN_BYTES`**；PBKDF2 见 **`Types.h`** |
| `AuthWithLocalPassword` | authWithLocalPassword | userId, passwordUtf8, clientIp(可空) | `bytes`（空=null） | **无 caller**；与 **`Auth`** 同源限流/封禁；成功返回 **sessionId** |
| `HasLocalPassword` | hasLocalPassword | userId | `bool` | **无 caller** |
| `ChangeLocalPassword` | changeLocalPassword | caller, userId, oldPasswordUtf8, newPasswordUtf8 | `bool` | **`userId`＝principal** |
| `ResetLocalPasswordWithRecovery` | resetLocalPasswordWithRecovery | userId, recoverySecretUtf8, newPasswordUtf8, clientIp(可空) | `bool` | **无 caller**；成功**不**自动签发 session |

### 一.2 RTC 呼叫状态（**无媒体面**）

| C++ | 逻辑（camelCase） | 输入 | 输出 | 说明 |
|-----|-------------------|------|------|------|
| `RtcStartCall` | rtcStartCall | caller, peerUserId, callKind(`int32`) | `bytes`（空=null） | **principal** 为发起方；**callKind**：**0=音频** **1=视频**（同 **`RtcCallSessionManager.h`**）；返回 **16B callId** |
| `RtcAcceptCall` | rtcAcceptCall | caller, callId | `bool` | 仅 **peer** 在 **RINGING** 时可接 |
| `RtcRejectCall` | rtcRejectCall | caller, callId | `bool` | 发起方或 peer |
| `RtcEndCall` | rtcEndCall | caller, callId | `bool` | |
| `RtcGetCallState` | rtcGetCallState | caller, callId | `int32` | 常量见 **`RtcCallSessionManager.h`** |
| `RtcGetCallKind` | rtcGetCallKind | caller, callId | `int32` | 失败 **-1** |

## 二、消息模块

| C++ | 逻辑（camelCase） | 输入 | 输出（C++） | 说明 |
|-----|-------------------|------|-------------|------|
| `StoreMessage` | storeMessage | caller, imSessionId, payload | `bytes`（空=null） | msgId；**`JniBridge`** 将 **`TryBindCaller` 得到的 `principal`** 作为 **`senderUserId`** 写入 **MM2 RAM IM 行**（见 **`03-Storage.md` 第2.6节**）。**须**与后续 **`editMessage` / `recallMessage`** 的 **`senderId`** 一致方可通过 MM1 校验。 |
| `RetrieveMessage` | retrieveMessage | caller, messageId | `bytes`（空=null） | |
| `DeleteMessage` | deleteMessage | caller, messageId, senderId, signatureEd25519 | `bool` | **`senderId`＝principal**；**MM1 `MessageRecallManager`**：**RAM `senderUserId`** 须与 **`senderId`** 一致后再 **Ed25519** 验签（**`ZChatIM|RecallMessage|v1`**）；见 **`03-Storage.md` 第2.6节**。 |
| `RecallMessage` | recallMessage | caller, messageId, senderId, signatureEd25519 | `bool` | 同 **`DeleteMessage`**（与 Recall 等价路径）。 |
| `ListMessages` | listMessages | caller, userId, count | `bytes[]` | 经 **`GetMessageQueryManager`**；**`userId`** 与 **`imSessionId` 同长（16B）**；**每元素**：**`message_id(16)‖payload_len(uint32 BE)‖payload`**（见 **`MessageQueryManager.h`** / **`03-Storage.md` 第七节**）。**空数组**亦可能表示 **caller 无效**或 **`principal≠userId`**（**`JniBridge`** 在此路径**不**调 MM2，**`MM2::LastError()` 可能仍为旧值**）。 |
| `ListMessagesSinceTimestamp` | listMessagesSinceTimestamp | caller, userId, sinceTimestampMs, count | `bytes[]` | **`count<=0`**：空集且不改 **`LastError`**。**`count>0`**：须 **`Initialize`**、**`userId`** 16B、解密就绪；按 **RAM `stored_at_ms >= sinceTimestampMs`** 取至多 **`count`** 条（**`03-Storage.md` 第2.6节**）。**`sinceTimestampMs`** 大于 **`int64_t` 最大** 时在实现侧按 **`INT64_MAX`** 截断。前置校验失败时空数组语义同 **`ListMessages`**。 |
| `ListMessagesSinceMessageId` | listMessagesSinceMessageId | caller, userId, lastMsgId, count | `bytes[]` | **`lastMsgId` 空**=从会话最早起 **`count`** 条；**非空**=严格晚于该 id 的后 **`count`** 条；编码同 **`ListMessages`**。前置校验失败时空数组语义同 **`ListMessages`**。 |
| `MarkMessageRead` | markMessageRead | caller, messageId, readTimestampMs | `bool` | |
| `GetUnreadSessionMessageIds` | getUnreadSessionMessageIds | caller, imSessionId, limit | `bytes[]` | 每元素为 **16B `messageId`**。native 调用 **`MM2::GetUnreadSessionMessages`**；配对第二元当前为 **0**（未读占位），JNI 仅输出 id 列表即可（见 **`MM2.h`** / **`ZChatIM/docs/JNI-API-Documentation.md`** 第2.2节） |
| `StoreMessageReplyRelation` | storeMessageReplyRelation | caller, **senderEd25519PublicKey(32B)**, messageId, repliedMsgId, repliedSenderId, repliedContentDigest, senderId, signatureEd25519(64B) | `bool` | 先 **MM1**（**`TryGetSessionUserId`** 绑定 principal==senderId + **`common::Ed25519VerifyDetached`**：**OpenSSL**）再 MM2 |
| `GetMessageReplyRelation` | getMessageReplyRelation | caller, messageId | `bytes[]` | 三元组各为一行元素 |
| `EditMessage` | editMessage | caller, messageId, newEncryptedContent, editTimestampSeconds, signature, senderId | `bool` | **`senderId`＝principal**；**`sender_user_id`** 须与 **`senderId`** 一致后再验签；**OpenSSL Ed25519** 验签；**公钥**来自 **`UserData` `type=0x45444A31`**。**Canonical（无分隔符拼接）**：ASCII **`ZChatIM|EditMessage|v1`** ‖ **`messageId`(16)** ‖ **`senderId`(16)** ‖ **`editTimestampSeconds` u64 BE** ‖ **`SHA-256(newEncryptedContent)`(32)**。规则：**`edit_count<3`**；若已有编辑则 **`editTimestampSeconds ≥ last`** 且 **间隔 ≤300s**（秒）；成功则 **`MM2::EditMessage`**（**`newEditCount = c+1`**）。**`newEncryptedContent`** 单条上限见 **`ZDB_MAX_WRITE_SIZE`**。 |
| `GetMessageEditState` | getMessageEditState | caller, messageId | `bytes` | **12 字节大端**：editCount[4]+lastEditTime[8] |

## 三、用户数据模块

| C++ | 逻辑（camelCase） | 输入 | 输出（C++） | 说明 |
|-----|-------------------|------|-------------|------|
| `StoreUserData` | storeUserData | caller, userId, type(int32), data | `bool` | **`JniBridge`** → **`UserDataManager`** → **`MM2::StoreMm1UserDataBlob`**；**MM2 已 Initialize** 时落 **`mm1_user_kv`**（**`03-Storage.md` 第2.6节**；元库 **`PRAGMA user_version=11`**）；**`userId`** 须与 **principal** 一致（见绑定矩阵）。 |
| `GetUserData` | getUserData | caller, userId, type(int32) | `bytes`（空=null） | 无行或空 BLOB 时返回**空向量**；无效 **caller** 或 **principal** 不匹配时亦为空/`Store` 为 false。 |
| `DeleteUserData` | deleteUserData | caller, userId, type(int32) | `bool` | 删除成功为 **true**；无匹配行时为 **false**（非异常）。 |

## 四、好友模块

| C++ | 逻辑（camelCase） | 输入 | 输出（C++） | 说明 |
|-----|-------------------|------|-------------|------|
| `SendFriendRequest` | sendFriendRequest | caller, fromUserId, toUserId, timestampSeconds, signatureEd25519 | `bytes`（空=null） | requestId |
| `RespondFriendRequest` | respondFriendRequest | caller, requestId, accept, responderId, timestampSeconds, signatureEd25519 | `bool` | |
| `DeleteFriend` | deleteFriend | caller, userId, friendId, timestampSeconds, signatureEd25519 | `bool` | |
| `GetFriends` | getFriends | caller, userId | `bytes[]` | |

## 五、群组模块

| C++ | 逻辑（camelCase） | 输入 | 输出（C++） | 说明 |
|-----|-------------------|------|-------------|------|
| `CreateGroup` | createGroup | caller, creatorId, name(string) | `bytes`（空=null） | **`creatorId` 须与 principal 一致**（绑定矩阵）；**`name`** 非空、**UTF-8 字节数 ≤2048**；成功返回 **16B `groupId`** |
| `InviteMember` | inviteMember | caller, groupId, userId | `bool` | **principal** = 邀请者（须群内 **admin/owner**）；**`userId`** = 被邀请人；**实现**校验 **`ListAcceptedFriendPeerUserIds(inviter)`** 含 **`userId`**（与 **`GroupManager.cpp`** 一致）。正常 **status=1** 单行下与「双向 accepted」等价；**不可**自邀（见文首「与仅 caller 并存」说明） |
| `RemoveMember` | removeMember | caller, groupId, userId | `bool` | **principal** = 执行者（须 **owner**）；**不可**踢自己（用 **`LeaveGroup`**） |
| `LeaveGroup` | leaveGroup | caller, groupId, userId | `bool` | **`userId` 须与 principal 一致**（绑定矩阵） |
| `GetGroupMembers` | getGroupMembers | caller, groupId | `bytes[]` | **principal** 须已是群成员；每元素 **16B `user_id`** |
| `UpdateGroupKey` | updateGroupKey | caller, groupId | `bool` | **principal** 须为群内 **owner/admin**；轮换写入 **`ZGK1`** 信封至 **`.zdb` + `group_data`**（见 **`03-Storage.md`** / **`MM2::UpsertGroupKeyEnvelopeForMm1`**） |

## 六、群聊安全特性（Mention / Mute / GroupName）

| C++ | 逻辑（camelCase） | 输入 | 输出（C++） | 说明 |
|-----|-------------------|------|-------------|------|
| `ValidateMentionRequest` | validateMentionRequest | caller, groupId, senderId, mentionType(int32), mentionedUserIds, nowMs, signatureEd25519 | `bool` | **`senderId`＝principal**；**公钥 `0x45444A31`**。**Canonical**：**`ZChatIM|MentionRequest|v1`** ‖ **`groupId`(16)** ‖ **`senderId`(16)** ‖ **`mentionType` i32 BE** ‖ **`nowMs` u64 BE** ‖ **`count` i32 BE**（**`mentionedUserIds.size()`**）‖ 各 **`mentionedUserId`(16)**。须 **发送者在群**（**`group_members` SQL**）。**`mentionType=1`**：**@用户**，**`mentionedUserIds` 非空**且均在群。**`mentionType=2`**：**@ALL**，发送者须 **owner/admin**，且校验 **60s** 滑动窗内已记录次数 **&lt;3**（与 **`RecordMentionAtAllUsage`** 同源 **`mm1_mention_atall_window`**；**进程重启可恢复**，须 **`MM2::Initialize`**）。 |
| `RecordMentionAtAllUsage` | recordMentionAtAllUsage | caller, groupId, senderId, nowMs | `bool` | **`senderId`＝principal**；须 **发送者在群**；向 **(groupId,senderId)** 的 **60s** 窗追加 **`nowMs`**，若窗内已有 **≥3** 条则 **false**。**建议**仅在 **`validateMentionRequest`（mentionType=2）** 通过后调用，与校验共用 **`mm1_mention_atall_window`** 状态。 |
| `MuteMember` | muteMember | caller, groupId, userId, mutedBy, startTimeMs, durationSeconds(int64), reason | `bool` | **`mutedBy`＝principal**；**群内 owner/admin**；**admin 仅可禁 member**；**不可**禁 **owner**、**不可自禁**；**`durationSeconds=-1`** 永久，**正** 限时，**0/其他非法** 失败；**`reason` ≤4096B**；**`JniBridge` → `GroupMuteManager`** → **`mm2_group_mute`** |
| `IsMuted` | isMuted | caller, groupId, userId, nowMs | `bool` | **仅校验 caller**；**`nowMs < start_ms`** 视为未禁言；永久时内部 **remaining=-1**；JNI 仅 bool |
| `UnmuteMember` | unmuteMember | caller, groupId, userId, unmutedBy | `bool` | **`unmutedBy`＝principal**；**群内 owner/admin**；目标须**已有**禁言行 |
| `UpdateGroupName` | updateGroupName | caller, groupId, updaterId, newGroupName, nowMs | `bool` | **`updaterId`＝principal**；**群内 owner/admin**；**`newGroupName`** 非空、**UTF-8 ≤2048**；**`MM2::UpdateGroupName`**（**`updated_s`＝`nowMs/1000`**） |
| `GetGroupName` | getGroupName | caller, groupId | `std::string` | **仅 caller**；**`MM2::GetGroupName`** |

## 七、文件模块（含续传断点）

| C++ | 逻辑（camelCase） | 输入 | 输出（C++） | 说明 |
|-----|-------------------|------|-------------|------|
| `StoreFileChunk` | storeFileChunk | caller, fileId(string), chunkIndex(**int**), data | `bool` | `chunkIndex` 须 **≥ 0** 且与 `data_blocks.chunk_idx` 一致；负值在 C++ 侧按 `uint32_t` 解释后会触发「过大」校验失败。`fileId` 编码须与 **`SHA256` 派生 `data_id` 所用字节** 一致（通常为 UTF-8）。分片大小受 **`ZDB_MAX_WRITE_SIZE`** 限制（见 `Types.h` / `03-Storage.md` 第七节）。 |
| `GetFileChunk` | getFileChunk | caller, fileId(string), chunkIndex(**int**) | `bytes`（空=null） | |
| `CompleteFile` | completeFile | caller, fileId(string), sha256(bytes) | `bool` | |
| `CancelFile` | cancelFile | caller, fileId(string) | `bool` | |
| `StoreTransferResumeChunkIndex` | storeTransferResumeChunkIndex | caller, fileId(string), chunkIndex(**uint32**) | `bool` | |
| `GetTransferResumeChunkIndex` | getTransferResumeChunkIndex | caller, fileId(string) | **`uint32_t`** | 失败语义见文首「路由摘要」 |
| `CleanupTransferResumeChunkIndex` | cleanupTransferResumeChunkIndex | caller, fileId(string) | `bool` | |

## 八、会话与多设备

| C++ | 逻辑（camelCase） | 输入 | 输出（C++） | 说明 |
|-----|-------------------|------|-------------|------|
| `GetSessionMessages` | getSessionMessages | caller, imSessionId, limit(int) | `bytes[]` | Native **`MM2::GetSessionMessages`** 返回 **`vector<pair<message_id,payload>>`**（**`limit==0`** 空集；顺序见 **`03-Storage.md` 第2.6节 / 第七节**）。JNI 打包建议：与 **`ListMessages`** 每行一致，即 **`message_id(16)‖lenBE32‖payload`** 串联多条，或自定义长度表；见 **`MessageQueryManager.h`**。 |
| `GetSessionStatus` | getSessionStatus | caller, imSessionId | `bool` | active=true |
| `TouchSession` | touchSession | caller, imSessionId, nowMs | `void` | |
| `CleanupExpiredSessions` | cleanupExpiredSessions | caller, nowMs | `void` | 定时/运维须持有效 caller |
| `RegisterDeviceSession` | registerDeviceSession | caller, userId, deviceId, sessionId, loginTimeMs, lastActiveMs | `bool` + `outKicked`；Java：`null`=失败；**`byte[0]`**=成功无踢；**16B**=被踢会话 id | **id 向量 16B**，见文首 ID 说明 |
| `UpdateLastActive` | updateLastActive | caller, userId, sessionId, nowMs | `bool` | |
| `GetDeviceSessions` | getDeviceSessions | caller, userId | `bytes[]` | 每元素 **48B 大端** |
| `CleanupExpiredDeviceSessions` | cleanupExpiredDeviceSessions | caller, nowMs | `void` | |
| `GetUserStatus` | getUserStatus | caller, userId | `bool` | **`UserStatusManager`** → **`mm1_user_status`**（**最后已知**；**服务端**权威；须 **`MM2::Initialize`**）；无行或 offline 为 **false** |
| `CleanupSessionMessages` | cleanupSessionMessages | caller, imSessionId | `bool` | |

## 九、数据清理与状态查询

| C++ | 逻辑（camelCase） | 输入 | 输出（C++） | 说明 |
|-----|-------------------|------|-------------|------|
| `CleanupExpiredData` | cleanupExpiredData | caller | `bool` | |
| `OptimizeStorage` | optimizeStorage | caller | `bool` | |
| `GetStorageStatus` | getStorageStatus | caller | `mapSS` | |
| `GetMessageCount` | getMessageCount | caller | `int64_t` | |
| `GetFileCount` | getFileCount | caller | `int64_t` | |

## 十、安全模块（密钥/运维/证书固定/注销/好友备注）

| C++ | 逻辑（camelCase） | 输入 | 输出（C++） | 说明 |
|-----|-------------------|------|-------------|------|
| `GenerateMasterKey` | generateMasterKey | caller | `bytes` | **`MM1::GenerateMasterKey()`**：随机生成并 **`StoreMasterKey`**；返回材料与槽内一致 |
| `RefreshSessionKey` | refreshSessionKey | caller | `bytes` | **`MM1::RefreshSessionKey()`**（锁内 `KeyManagement`） |
| `EmergencyWipe` | emergencyWipe | caller | `void` | 高危；**`JniBridge`** → **`MM1::EmergencyTrustedZoneWipe`**（**`MM2::CleanupAllData`** + **Auth 全表** + **多设备登记** + **在线状态** + **证书 Pin/封禁** + **IM 会话活跃表** + **@ALL 限速** + **主密钥** + **`MM1::Cleanup`**）+ 桥接 **`m_initialized.store(false, release)`**（**`std::atomic<bool>`**）。**纯 C++** 若仅调 **`SystemControl::EmergencyWipe`** 则**不**改 **`JniBridge::m_initialized`**（见 **`01-MM1.md`**）。 |
| `GetStatus` | getStatus | caller | `mapSS` | **`jni_bridge_initialized` / `mm2_initialized` / `mm1_master_key_present`**（`0`/`1`）；**JNI 直连 `JniBridge`**（键与 **`MM1::SystemControlStatusSnapshot`** 不同：后者为 **`mm1_initialized`** 等，无桥接键） |
| `RotateKeys` | rotateKeys | caller | `bool` | **`MM1::RefreshMasterKey()`**（锁内）；**JNI 不经 `GetSystemControl()`**（与纯 C++ **`SystemControl::RotateKeys`** 语义等价） |
| `ConfigurePinnedPublicKeyHashes` | configurePinnedPublicKeyHashes | caller, currentSpkiSha256, standbySpkiSha256 | `void` | |
| `VerifyPinnedServerCertificate` | verifyPinnedServerCertificate | caller, clientId, presentedSpkiSha256 | `bool` | caller 空见文首 TLS 说明 |
| `IsClientBanned` | isClientBanned | caller, clientId | `bool` | |
| `RecordFailure` | recordFailure | caller, clientId | `void` | caller 空见文首 TLS 说明 |
| `ClearBan` | clearBan | caller, clientId | `void` | |
| `DeleteAccount` | deleteAccount | caller, userId, reauthToken, secondConfirmToken | `bool` | **`userId`＝principal**。**`reauthToken` 与 `secondConfirmToken` 须同长度 ≥16B 且逐字节相同**（**`ConstantTimeCompare`**）。写入 **`mm1_user_kv`** **`type=0x41434431`（'ACD1'）** 数据 **`{1}`** 作墓碑；**不**调用 **`CleanupAllData`** / 不全库擦除。 |
| `IsAccountDeleted` | isAccountDeleted | caller, userId | `bool` | 读 **'ACD1'** 墓碑；首字节非 **0** 为已注销标记。 |
| `UpdateFriendNote` | updateFriendNote | caller, userId, friendId, newEncryptedNote, updateTimestampSeconds, signatureEd25519 | `bool` | **`userId`＝principal**；**公钥 `0x45444A31`**。**Canonical**：**`ZChatIM|UpdateFriendNote|v1`** ‖ **`userId`(16)** ‖ **`friendId`(16)** ‖ **`updateTimestampSeconds` u64 BE** ‖ **`SHA-256(newEncryptedNote)`(32)**。须 **`friendId`** 在 **`GetFriends(userId)`**（accepted）中。持久化 **`mm1_user_kv`** **`type=0x464E424E`（'FNBN'）**：**`ZFN1`** 魔数 + 重复 **`friendId(16)‖len u32 BE‖note`**；单条 **`note` ≤64KiB**。 |

## 十一、JNI 调用验证（C++ 重载）

| C++ | 逻辑（camelCase） | 输入 | 输出（C++） | 说明 |
|-----|-------------------|------|-------------|------|
| `ValidateJniCall()` | validateJniCall | - | `bool` | 退化路径：**`JniBridge::m_initialized.load()`**（**atomic**；持 **`m_apiRecursiveMutex`**）；不得作为唯一安全边界 |
| `ValidateJniCall(const void* jniEnv, const void* jclass)` | validateJniCall（强校验） | jniEnv, jclass | `bool` | 优先；转发 `MM1::ValidateJniCall` / `JniSecurity` |

## 十二、错误码

**说明**：下列为逻辑/协议层错误码。**`JniInterface` 不直接返回本枚举**；Java 层按返回值与安全策略映射。若需统一错误码出口，须在 JNI 层另增约定。

| 错误码 | 说明 |
|--------|------|
| E_SUCCESS | 成功 |
| E_AUTH_FAILED | 认证失败 |
| E_AUTH_LOCKED | 账户已锁定 |
| E_AUTH_RATE_LIMIT | 频率超限 |
| E_INVALID_SESSION | 无效会话 |
| E_SESSION_EXPIRED | 会话过期 |
| E_NOT_FOUND | 资源不存在 |
| E_PERMISSION_DENIED | 权限拒绝 |
| E_INVALID_DATA | 数据无效 |
| E_STORAGE_FAILED | 存储失败 |
| E_ENCRYPT_FAILED | 加密失败 |
| E_DECRYPT_FAILED | 解密失败 |
