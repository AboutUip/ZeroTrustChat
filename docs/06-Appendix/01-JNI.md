# JNI 接口清单

## 类型与长度（`ZChatIM/include/Types.h`）

| 符号 | 字节 | JNI/Java 常见形态 |
|------|-----:|-------------------|
| `USER_ID_SIZE` | 16 | `byte[16]`：userId、messageId、groupId、callId、imSessionId 等 |
| `JNI_AUTH_SESSION_TOKEN_BYTES` | 16 | 同左；caller / Auth 返回值 |
| `MESSAGE_ID_SIZE` | 16 | 消息 id |
| `SESSION_ID_SIZE` | 4 | 仅 ZSP 帧头，非 imSessionId |
| `AUTH_OPAQUE_CREDENTIAL_MIN_BYTES` | 32 | Auth token 下限；细则 `AuthSessionManager` |
| `SHA256_SIZE` | 32 | 摘要、canonical |
| Ed25519 公钥 / 签名 | 32 / 64 | 回复链、编辑、撤回等 |
| `ZDB_MAX_WRITE_SIZE` | 500×1024 | 单条密文载荷上限 |
| `FILE_CHUNK_SIZE` | 65536 | 分片默认大小 |
| `MM1_USER_KV_TYPE_AVATAR_V1` | — | `int32`=`0x41565431`（ASCII `AVT1`）；头像原图 blob，见用户数据模块 |
| `MM1_USER_KV_TYPE_DISPLAY_NAME_V1` | — | `int32`=`0x4E4D4E31`（ASCII `NMN1`）；UTF-8 展示昵称；`MM1_USER_DISPLAY_NAME_MAX_BYTES`=256 |
| `MM1_USER_AVATAR_MAX_BYTES` | 65535 | 与 ZSP 单帧 Payload 上限一致；`StoreUserData` 在 `type==AVT1` 时强制 |
| `MM1_USER_DISPLAY_NAME_MAX_BYTES` | 256 | `StoreUserData` 在 `type==NMN1` 时强制 |

`bytes` = `std::vector<uint8_t>`；`bytes[]` = 其向量；`mapSS` = `map<string,string>`。列表行编码见 `MessageQueryManager.h`。

---

**P0**：`kNativeMethods[]` 与 `ZChatIMNative.java` 须 1:1（含 `initializeWithPassphrase`、`lastInitializeError`、本地账户、`rtc*`、`validateJniCall` 两签名）；否则 `JNI_OnLoad` 失败。

**维护**：`C++` 列 ↔ `JniInterface.h` / `JniBridge.h`；流程见 [`JNI-API-Documentation.md`](../../ZChatIM/docs/JNI-API-Documentation.md) 第0节。改 API 须同步本文、该文档、头文件与 Java。

**caller**：首参 `callerSessionId`（16B，同 `Auth` 返回值），下文记为 caller。无 caller：`Initialize`、`InitializeWithPassphrase`、`lastInitializeError`、`Cleanup`、`Auth`、`VerifySession`、`RegisterLocalUser`、`AuthWithLocalPassword`、`HasLocalPassword`、`ResetLocalPasswordWithRecovery`、`ValidateJniCall`（两重载）。其余经 `TryBindCaller`；已 `Initialize` 时与 `VerifySession(caller)` 对同一会话等价。`imSessionId` 为 16B IM 通道 id，与 ZSP 头 4 字节 `SESSION_ID_SIZE` 无关。`DestroySession`：两会话 id 均须绑定成功且 principal 相同。TLS：`VerifyPinnedServerCertificate`、`RecordFailure` 见 [`02-ZSP-Protocol.md`](../01-Architecture/02-ZSP-Protocol.md) 第十节与 `JniSecurityPolicy.h`（caller 可空规则以实现为准）。

**并发**：`m_apiRecursiveMutex`（桥接）、`MM1::m_apiRecursiveMutex`、`MM2::m_stateMutex`；`m_initialized` 语义见 `JniSecurityPolicy.h` 第5节。`Get*Manager()` 仅保证取引用瞬间持锁；链式调用见 `JNI-API-Documentation.md` 第0节第7条。`MessageQueryManager` 仅在 MM2 已持 `m_stateMutex` 时使用。

**Auth 的 `clientIp`**：可空；非空启用 IP 限流与 `userId‖IP` 封禁（[`02-Auth.md`](../03-Business/02-Auth.md) 第七节）。

**路由**：各 API 落点见 [`JNI-API-Documentation.md`](../../ZChatIM/docs/JNI-API-Documentation.md)。**续传索引**：`GetTransferResumeChunkIndex` 失败时 C++ 为 `UINT32_MAX`，JNI 转 `jint` 后 Java 为 -1。

**ID**：`SESSION_ID_SIZE`=4 仅 ZSP 帧头；`RegisterDeviceSession` 等 `deviceId`/`sessionId` 向量均为 16B（同 `USER_ID_SIZE`），非 ZSP 头字段。

### principal 绑定矩阵（与 `JniBridge.cpp` 一致）

「+ principal 绑定」：caller 有效且 `principal` 与表中入参 16B id 经 `ConstantTimeCompare` 相等。「仅 caller」：仅 `TryBindCaller` 等前置，不要求 `imSessionId == principal`。群 Invite/Remove/GetMembers/UpdateGroupKey 在 JNI 不对入参 `userId` 做 PrincipalMatches；操作者与角色在 `GroupManager` / SQLite。非「不使用 principal」。

| 规则 | C++ API（`JniInterface` / `JniBridge` 同名） |
|------|---------------------------------------------|
| 双会话同一 principal | `DestroySession` |
| caller 有效且 principal 等于该 API 表中标定的 16B 入参 | `ListMessages` / `ListMessagesSinceTimestamp` / `ListMessagesSinceMessageId`（**`userId`**）；`DeleteMessage` / `RecallMessage`（**`senderId`**）；`StoreUserData` / `DeleteUserData`（**`userId`**）；`GetUserData`（**`userId`**，**例外见下**）；`SendFriendRequest`（**`fromUserId`**）；`RespondFriendRequest`（**`responderId`**）；`DeleteFriend` / `GetFriends`（**`userId`**）；`CreateGroup`（**`creatorId`**）；`LeaveGroup`（**`userId`**）；`ValidateMentionRequest` / `RecordMentionAtAllUsage`（**`senderId`**）；`MuteMember`（**`mutedBy`**）；`UnmuteMember`（**`unmutedBy`**）；`UpdateGroupName`（**`updaterId`**）；`RegisterDeviceSession` / `UpdateLastActive` / `GetDeviceSessions`（**`userId`**）；`GetUserStatus`（**`userId`**）；`DeleteAccount` / `IsAccountDeleted`（**`userId`**）；`UpdateFriendNote`（**`userId`**）；`EditMessage`（**`senderId`**）；**`ChangeLocalPassword`（`userId`）** |
| **`GetUserData` 例外** | `type == MM1_USER_KV_TYPE_AVATAR_V1`（`AVT1`）或 `type == MM1_USER_KV_TYPE_DISPLAY_NAME_V1`（`NMN1`）时：**任意已登录用户均可读取**（用于添加好友搜索功能）；其余 `type` 仍须 `principal == userId` |
| **仅 caller**（不比对 principal 与 `imSessionId` / 消息 id） | `StoreMessage`, `RetrieveMessage`, `MarkMessageRead`, `GetUnreadSessionMessageIds`, `GetSessionMessages`, `GetSessionStatus`, `TouchSession`, `CleanupExpiredSessions`, `CleanupSessionMessages`, `InviteMember`, `RemoveMember`, `GetGroupMembers`, `UpdateGroupKey`, `IsMuted`, `GetGroupName`, `StoreFileChunk`, `GetFileChunk`, `CompleteFile`, `CancelFile`, `StoreTransferResumeChunkIndex`, `GetTransferResumeChunkIndex`, `CleanupTransferResumeChunkIndex`, `GetMessageReplyRelation`, `GetMessageEditState`, `CleanupExpiredDeviceSessions`, `CleanupExpiredData`, `OptimizeStorage`, `GetStorageStatus`, `GetMessageCount`, `GetFileCount`, `GenerateMasterKey`, `RefreshSessionKey`, `EmergencyWipe`, `GetStatus`, `RotateKeys`, `ConfigurePinnedPublicKeyHashes`, `IsClientBanned`, `ClearBan`, **`RtcStartCall`**, **`RtcAcceptCall`**, **`RtcRejectCall`**, **`RtcEndCall`**, **`RtcGetCallState`**, **`RtcGetCallKind`** |
| **`caller` 可空**：空则跳过 `TryBindCaller`；非空则须有效 | `VerifyPinnedServerCertificate`, `RecordFailure` |
| 桥接层**不**调用 `TryBindCaller`；**`MessageReplyManager`** 内 **`TryGetSessionUserId` + `senderId`** | `StoreMessageReplyRelation` |

---

## 零、生命周期

| C++ | 逻辑（camelCase） | 输入 | 输出（C++） | 说明 |
|-----|-------------------|------|-------------|------|
| `Initialize` | initialize | dataDir(string), indexDir(string) | `bool` | MM1+MM2；路径非空。幂等与路径一致性见 `JniBridge::Initialize`、`JNI-API-Documentation.md` 第2节（生命周期） |
| `InitializeWithPassphrase` | initializeWithPassphrase | dataDir(string), indexDir(string), messageKeyPassphraseUtf8(`const char*`，可 null；非 null 须非空 C 串) | `bool` | MM2+ZMKP；须 SQLCipher。`nullptr`/Java `null` 同 `Initialize` |
| `LastInitializeError` | lastInitializeError | - | `string`（Java `null` 当空） | 最近一次 `Initialize`/`InitializeWithPassphrase` 失败说明（`JniBridge` 或 `MM2::LastError`）；成功或未调用时为空 |
| `Cleanup` | cleanup | - | `void` | MM2::Cleanup → 清 Auth → MM1::Cleanup → Notify*；与 EmergencyWipe 出口见 `JniSecurityPolicy.h` 第8节 |

## 一、认证模块

| C++ | 逻辑（camelCase） | 输入 | 输出（C++） | 说明 |
|-----|-------------------|------|-------------|------|
| `Auth` | auth | userId, token, clientIp(bytes, 可空) | `bytes`（空=null） | sessionId；须 Initialize 成功后（否则空；[`02-Auth.md`](../03-Business/02-Auth.md) 第7.3节）。userId 16B；token 规则见 `AuthSessionManager::VerifyCredential` |
| `VerifySession` | verifySession | sessionId | `bool` | active=true |
| `DestroySession` | destroySession | caller, sessionIdToDestroy | `bool` | 见文首 caller 策略 |

### 一.1 本地账户（**mm1_user_kv** **LPH1/LRC1**）

| C++ | 逻辑（camelCase） | 输入 | 输出（C++） | 说明 |
|-----|-------------------|------|-------------|------|
| `RegisterLocalUser` | registerLocalUser | userId, passwordUtf8, recoverySecretUtf8 | `bool` | 无 caller；Initialize+MM2；口令/恢复密钥长度见 `Types.h` |
| `AuthWithLocalPassword` | authWithLocalPassword | userId, passwordUtf8, clientIp(可空) | `bytes`（空=null） | 无 caller；限流同 Auth |
| `HasLocalPassword` | hasLocalPassword | userId | `bool` | 无 caller |
| `ChangeLocalPassword` | changeLocalPassword | caller, userId, oldPasswordUtf8, newPasswordUtf8 | `bool` | userId＝principal |
| `ResetLocalPasswordWithRecovery` | resetLocalPasswordWithRecovery | userId, recoverySecretUtf8, newPasswordUtf8, clientIp(可空) | `bool` | 无 caller；成功不自动发 session |

### 一.2 RTC 状态（无媒体面）

| C++ | 逻辑（camelCase） | 输入 | 输出 | 说明 |
|-----|-------------------|------|------|------|
| `RtcStartCall` | rtcStartCall | caller, peerUserId, callKind(`int32`) | `bytes`（空=null） | 16B callId；callKind 0/1 见 `RtcCallSessionManager.h` |
| `RtcAcceptCall` | rtcAcceptCall | caller, callId | `bool` | peer 且 RINGING |
| `RtcRejectCall` | rtcRejectCall | caller, callId | `bool` | |
| `RtcEndCall` | rtcEndCall | caller, callId | `bool` | |
| `RtcGetCallState` | rtcGetCallState | caller, callId | `int32` | 常量见头文件 |
| `RtcGetCallKind` | rtcGetCallKind | caller, callId | `int32` | 失败 -1 |

## 二、消息模块

| C++ | 逻辑（camelCase） | 输入 | 输出（C++） | 说明 |
|-----|-------------------|------|-------------|------|
| `StoreMessage` | storeMessage | caller, imSessionId, payload | `bytes`（空=null） | msgId；principal→RAM `senderUserId`；须与后续 edit/recall 的 senderId 一致（[`03-Storage.md`](../02-Core/03-Storage.md) 第2.6节） |
| `RetrieveMessage` | retrieveMessage | caller, messageId | `bytes`（空=null） | |
| `DeleteMessage` | deleteMessage | caller, messageId, senderId, signatureEd25519 | `bool` | senderId＝principal；Recall canonical + Ed25519；Storage 第2.6节 |
| `RecallMessage` | recallMessage | caller, messageId, senderId, signatureEd25519 | `bool` | 同 DeleteMessage |
| `ListMessages` | listMessages | caller, userId, count | `bytes[]` | userId 16B；行 `message_id‖lenBE32‖payload`。空数组：caller 无效或 principal≠userId 等；此路径不调 MM2，`LastError` 可能陈旧 |
| `ListMessagesSinceTimestamp` | listMessagesSinceTimestamp | caller, userId, sinceTimestampMs, count | `bytes[]` | count≤0 空集不改 LastError；since 过大截 INT64_MAX；失败语义同 ListMessages |
| `ListMessagesSinceMessageId` | listMessagesSinceMessageId | caller, userId, lastMsgId, count | `bytes[]` | lastMsgId 空=从头 count 条；非空=严格晚于该 id；编码同 ListMessages |
| `MarkMessageRead` | markMessageRead | caller, messageId, readTimestampMs | `bool` | |
| `GetUnreadSessionMessageIds` | getUnreadSessionMessageIds | caller, imSessionId, limit | `bytes[]` | 每元素 16B messageId；见 `MM2.h`、`JNI-API-Documentation.md` 第2节（消息） |
| `StoreMessageReplyRelation` | storeMessageReplyRelation | caller, senderEd25519PublicKey(32B), messageId, repliedMsgId, repliedSenderId, repliedContentDigest, senderId, signatureEd25519(64B) | `bool` | MM1 验签+principal==senderId 后 MM2 |
| `GetMessageReplyRelation` | getMessageReplyRelation | caller, messageId | `bytes[]` | 三元组各行一元素 |
| `EditMessage` | editMessage | caller, messageId, newEncryptedContent, editTimestampSeconds, signature, senderId | `bool` | senderId＝principal；canonical 与次数/时间窗见 [`09-MessageEdit.md`](../04-Features/09-MessageEdit.md)；公钥 UserData `0x45444A31`；上限 `ZDB_MAX_WRITE_SIZE` |
| `GetMessageEditState` | getMessageEditState | caller, messageId | `bytes` | 12B BE：editCount[4]+lastEditTime[8] |

## 三、用户数据模块

| C++ | 逻辑（camelCase） | 输入 | 输出（C++） | 说明 |
|-----|-------------------|------|-------------|------|
| `StoreUserData` | storeUserData | caller, userId, type(int32), data | `bool` | **MM2 已初始化**时 **`mm1::UserDataManager` → `MM2::StoreMm1UserDataBlob` → SQLCipher `mm1_user_kv`** 持久化（与 LPH1 相同）；userId＝principal；**`type==AVT1`** 时 `data.size()≤MM1_USER_AVATAR_MAX_BYTES`；**`type==NMN1`** 时 `data.size()≤MM1_USER_DISPLAY_NAME_MAX_BYTES`（否则 false）；Storage 第2.6节 |
| `GetUserData` | getUserData | caller, userId, type(int32) | `bytes`（空=null） | 无行为空向量；**`type==AVT1` 或 `type==NMN1`** 时允许任意已登录用户查询（用于添加好友搜索）；其余 `type` 须 `principal == userId` |
| `DeleteUserData` | deleteUserData | caller, userId, type(int32) | `bool` | userId＝principal；无行 false |

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
| `CreateGroup` | createGroup | caller, creatorId, name(string) | `bytes`（空=null） | creatorId＝principal；name UTF-8 ≤2048；16B groupId |
| `InviteMember` | inviteMember | caller, groupId, userId | `bool` | principal=邀请者 admin/owner；userId=被邀请人；须已接受好友；不可自邀 |
| `RemoveMember` | removeMember | caller, groupId, userId | `bool` | principal owner；不可踢己，用 LeaveGroup |
| `LeaveGroup` | leaveGroup | caller, groupId, userId | `bool` | userId＝principal |
| `GetGroupMembers` | getGroupMembers | caller, groupId | `bytes[]` | principal 须为成员；每元素 16B user_id |
| `UpdateGroupKey` | updateGroupKey | caller, groupId | `bool` | owner/admin；ZGK1 见 Storage、`MM2::UpsertGroupKeyEnvelopeForMm1` |

## 六、群聊安全特性（Mention / Mute / GroupName）

| C++ | 逻辑（camelCase） | 输入 | 输出（C++） | 说明 |
|-----|-------------------|------|-------------|------|
| `ValidateMentionRequest` | validateMentionRequest | caller, groupId, senderId, mentionType(int32), mentionedUserIds, nowMs, signatureEd25519 | `bool` | senderId＝principal；细则见 [`12-Mention.md`](../04-Features/12-Mention.md) |
| `RecordMentionAtAllUsage` | recordMentionAtAllUsage | caller, groupId, senderId, nowMs | `bool` | @ALL 窗；同 [`12-Mention.md`](../04-Features/12-Mention.md) |
| `MuteMember` | muteMember | caller, groupId, userId, mutedBy, startTimeMs, durationSeconds(int64), reason | `bool` | mutedBy＝principal；规则见 [`11-GroupMute.md`](../04-Features/11-GroupMute.md) |
| `IsMuted` | isMuted | caller, groupId, userId, nowMs | `bool` | 仅 caller；永久/限时语义见 [`11-GroupMute.md`](../04-Features/11-GroupMute.md) |
| `UnmuteMember` | unmuteMember | caller, groupId, userId, unmutedBy | `bool` | unmutedBy＝principal；见 [`11-GroupMute.md`](../04-Features/11-GroupMute.md) |
| `UpdateGroupName` | updateGroupName | caller, groupId, updaterId, newGroupName, nowMs | `bool` | updaterId＝principal；见 [`13-GroupName.md`](../04-Features/13-GroupName.md) |
| `GetGroupName` | getGroupName | caller, groupId | `std::string` | 仅 caller |

## 七、文件模块（含续传断点）

| C++ | 逻辑（camelCase） | 输入 | 输出（C++） | 说明 |
|-----|-------------------|------|-------------|------|
| `StoreFileChunk` | storeFileChunk | caller, fileId(string), chunkIndex(**int**), data | `bool` | chunkIndex≥0；fileId 与 data_id 派生一致；单块 ≤`ZDB_MAX_WRITE_SIZE`（[`08-FileTransfer.md`](../04-Features/08-FileTransfer.md)） |
| `GetFileChunk` | getFileChunk | caller, fileId(string), chunkIndex(**int**) | `bytes`（空=null） | |
| `CompleteFile` | completeFile | caller, fileId(string), sha256(bytes) | `bool` | |
| `CancelFile` | cancelFile | caller, fileId(string) | `bool` | |
| `StoreTransferResumeChunkIndex` | storeTransferResumeChunkIndex | caller, fileId(string), chunkIndex(**uint32**) | `bool` | |
| `GetTransferResumeChunkIndex` | getTransferResumeChunkIndex | caller, fileId(string) | **`uint32_t`** | 失败见文首「续传索引」 |
| `CleanupTransferResumeChunkIndex` | cleanupTransferResumeChunkIndex | caller, fileId(string) | `bool` | |

## 八、会话与多设备

| C++ | 逻辑（camelCase） | 输入 | 输出（C++） | 说明 |
|-----|-------------------|------|-------------|------|
| `GetSessionMessages` | getSessionMessages | caller, imSessionId, limit(int) | `bytes[]` | limit=0 空集；打包宜同 ListMessages 行格式；顺序 Storage 第2.6节/第七节、`MessageQueryManager.h` |
| `GetSessionStatus` | getSessionStatus | caller, imSessionId | `bool` | active=true |
| `TouchSession` | touchSession | caller, imSessionId, nowMs | `void` | |
| `CleanupExpiredSessions` | cleanupExpiredSessions | caller, nowMs | `void` | |
| `RegisterDeviceSession` | registerDeviceSession | caller, userId, deviceId, sessionId, loginTimeMs, lastActiveMs | `bool` + `outKicked`；Java：`null` 失败；`byte[0]` 成功无踢；16B 被踢会话 id | id 均 16B，见文首 ID |
| `UpdateLastActive` | updateLastActive | caller, userId, sessionId, nowMs | `bool` | |
| `GetDeviceSessions` | getDeviceSessions | caller, userId | `bytes[]` | 每元素 48B BE |
| `CleanupExpiredDeviceSessions` | cleanupExpiredDeviceSessions | caller, nowMs | `void` | |
| `GetUserStatus` | getUserStatus | caller, userId | `bool` | `mm1_user_status` 缓存；无行/offline→false；多设备语义见 [`06-MultiDevice.md`](../04-Features/06-MultiDevice.md) |
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
| `GenerateMasterKey` | generateMasterKey | caller | `bytes` | MM1 主密钥 |
| `RefreshSessionKey` | refreshSessionKey | caller | `bytes` | MM1 锁内 KeyManagement |
| `EmergencyWipe` | emergencyWipe | caller | `void` | `EmergencyTrustedZoneWipe`+Notify*；范围与桥接标志见 [`01-MM1.md`](../02-Core/01-MM1.md)、`JniSecurityPolicy.h` 第8节 |
| `GetStatus` | getStatus | caller | `mapSS` | 桥接键：`jni_bridge_initialized` 等；与 `SystemControlStatusSnapshot` 键集不同 |
| `RotateKeys` | rotateKeys | caller | `bool` | 同 `MM1::RefreshMasterKey`（锁内） |
| `ConfigurePinnedPublicKeyHashes` | configurePinnedPublicKeyHashes | caller, currentSpkiSha256, standbySpkiSha256 | `void` | |
| `VerifyPinnedServerCertificate` | verifyPinnedServerCertificate | caller, clientId, presentedSpkiSha256 | `bool` | caller 空见文首 TLS 说明 |
| `IsClientBanned` | isClientBanned | caller, clientId | `bool` | |
| `RecordFailure` | recordFailure | caller, clientId | `void` | caller 空见文首 TLS 说明 |
| `ClearBan` | clearBan | caller, clientId | `void` | |
| `DeleteAccount` | deleteAccount | caller, userId, reauthToken, secondConfirmToken | `bool` | userId＝principal；双 token 同长≥16 且相同（ConstantTimeCompare）；ACD1 墓碑；不全库擦除 |
| `IsAccountDeleted` | isAccountDeleted | caller, userId | `bool` | ACD1 墓碑 |
| `UpdateFriendNote` | updateFriendNote | caller, userId, friendId, newEncryptedNote, updateTimestampSeconds, signatureEd25519 | `bool` | userId＝principal；见 [`05-FriendVerify.md`](../04-Features/05-FriendVerify.md) 第九节、[`03-Storage.md`](../02-Core/03-Storage.md) |

## 十一、JNI 调用验证（C++ 重载）

| C++ | 逻辑（camelCase） | 输入 | 输出（C++） | 说明 |
|-----|-------------------|------|-------------|------|
| `ValidateJniCall()` | validateJniCall | - | `bool` | `m_initialized.load()`；非唯一安全边界 |
| `ValidateJniCall(const void* jniEnv, const void* jclass)` | validateJniCall（强校验） | jniEnv, jclass | `bool` | 优先；`MM1::ValidateJniCall` / `JniSecurity` |

## 十二、错误码

逻辑层枚举与语义见 [`02-Auth.md`](../03-Business/02-Auth.md) 等。`JniInterface` 以各 API 返回值表达结果，Java 映射由上层约定。
