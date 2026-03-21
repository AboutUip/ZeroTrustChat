# JNI 接口清单

**严格对齐**：本表 **`C++` 列**与 **`ZChatIM/include/jni/JniInterface.h`** 中 `static` 方法名及重载一一对应；**`JniBridge.h`** 实例方法同名、同参数、同返回值（无 `static`）。**`逻辑（camelCase）` 列**供 Java/JNI 命名参考，与 C++ `PascalCase` 为同一契约。

**维护规则**：增删改任一 JNI 入口时，须**同步**更新本文件、`ZChatIM/docs/JNI-API-Documentation.md`、**`ZChatIM/include/jni/JniInterface.h`**、**`ZChatIM/include/jni/JniBridge.h`**。

**类型简写**（与头文件一致）：`bytes` = `std::vector<uint8_t>`；`bytes[]` = `std::vector<std::vector<uint8_t>>`；`mapSS` = `std::map<std::string,std::string>`。

**callerSessionId（最高安全性默认）**：
- 缩写 **`caller`** 表示首参 `callerSessionId`（`bytes`，长度 **`Types::JNI_AUTH_SESSION_TOKEN_BYTES`**，与 `Auth` 返回句柄一致）。
- **不含** `caller` 的 API：`Initialize` / `Cleanup` / `Auth` / `VerifySession` / `ValidateJniCall` 两重载。
- **其余全部**业务 API 首参均为 `caller`；实现 MUST：`VerifySession(caller)==true` 后解析 principal，并对后续 `userId` / `senderId` / `imSessionId` 等做绑定与授权（细则 `ZChatIM/include/common/JniSecurityPolicy.h`）。
- **`imSessionId`**：即时通讯**会话通道** ID（`StoreMessage` / `GetSessionMessages` / `TouchSession` 等），与 ZSP 头内 4 字节 `SESSION_ID_SIZE` **无关**。
- **`DestroySession`**：`caller` + `sessionIdToDestroy`；实现 MUST 校验销毁权限（本人或运维角色等）。
- **TLS 回调**：`VerifyPinnedServerCertificate` / `RecordFailure` 等仍带 `caller`；`caller` **可为空** 仅当实现可证明等价来源（见策略头文件）。

**并发（头文件契约）**：`JniBridge::m_apiRecursiveMutex`、`MM1::m_apiRecursiveMutex`、`MM2::m_stateMutex`（递归）；`MessageQueryManager` 仅在 MM2 已持 `m_stateMutex` 时使用。

**`Auth` 第三参 `clientIp`**：可为空；非空时 `AuthSessionManager` 启用 IP 级限流与 `userId‖IP` 封禁键（见 `docs/03-Business/02-Auth.md` 第七节）。

**路由摘要**（与 `JNI-API-Documentation.md` 一致）：
- `listMessages*` → `mm2::MM2::GetMessageQueryManager()`
- `updateGroupName` → `mm1::GroupNameManager` 校验后 → `mm2::MM2::UpdateGroupName`
- `getGroupName` → `mm2::MM2::GetGroupName`
- `deleteMessage` / `recallMessage` → `mm1::MessageRecallManager`（禁止直调 `MM2::DeleteMessage`）
- `refreshSessionKey` → `mm1::MM1::GetKeyManagement().RefreshSessionKey()`
- `getTransferResumeChunkIndex`：头文件返回 `uint32_t`；`MM2::GetTransferResumeChunkIndex` 为 `bool`+`out` 时，失败哨兵须实现约定（如 `UINT32_MAX`）

**ID 长度**：`Types.h` 中 `SESSION_ID_SIZE == 4` 表示 **ZSP 协议头**内 SessionID 字段（见 `docs/01-Architecture/02-ZSP-Protocol.md`）。**`RegisterDeviceSession` 等 JNI 参数**中 `deviceId` / `sessionId` 的 `vector<uint8_t>` 约定为 **16 字节**，与 **`USER_ID_SIZE` / `MESSAGE_ID_SIZE`** 同级二进制载荷语义一致，**不等于** ZSP 头 4 字节字段。

---

## 零、生命周期

| C++ | 逻辑（camelCase） | 输入 | 输出（C++） | 说明 |
|-----|-------------------|------|-------------|------|
| `Initialize` | initialize | - | `bool` | 初始化可信区与桥接 |
| `Cleanup` | cleanup | - | `void` | 释放资源 |

## 一、认证模块

| C++ | 逻辑（camelCase） | 输入 | 输出（C++） | 说明 |
|-----|-------------------|------|-------------|------|
| `Auth` | auth | userId, token, clientIp(bytes, **可空**) | `bytes`（空=null） | sessionId；`clientIp` 建议传 IPv4 4B/IPv6 16B 以启用 02-Auth IP 限流 |
| `VerifySession` | verifySession | sessionId | `bool` | active=true |
| `DestroySession` | destroySession | caller, sessionIdToDestroy | `bool` | 见文首 caller 策略 |

## 二、消息模块

| C++ | 逻辑（camelCase） | 输入 | 输出（C++） | 说明 |
|-----|-------------------|------|-------------|------|
| `StoreMessage` | storeMessage | caller, imSessionId, payload | `bytes`（空=null） | msgId |
| `RetrieveMessage` | retrieveMessage | caller, messageId | `bytes`（空=null） | |
| `DeleteMessage` | deleteMessage | caller, messageId, senderId, signatureEd25519 | `bool` | 对齐 MessageRecall |
| `RecallMessage` | recallMessage | caller, messageId, senderId, signatureEd25519 | `bool` | |
| `ListMessages` | listMessages | caller, userId, count | `bytes[]` | 经 **`GetMessageQueryManager`**；**`userId`** 与 **`imSessionId` 同长（16B）**；**每元素**：**`message_id(16)‖payload_len(uint32 BE)‖payload`**（见 **`MessageQueryManager.h`** / **`03-Storage.md` 第七节**） |
| `ListMessagesSinceTimestamp` | listMessagesSinceTimestamp | caller, userId, sinceTimestampMs, count | `bytes[]` | **`count<=0`**：空集且不改 **`LastError`**。**`count>0`**：须 **`Initialize`** 且 **`userId`** 16B；否则 **`LastError`** 为未初始化/长度错。已就绪时 native **仍返回空**，**`LastError`** 为 **not supported**（**`im_messages` 无时间列**） |
| `ListMessagesSinceMessageId` | listMessagesSinceMessageId | caller, userId, lastMsgId, count | `bytes[]` | **`lastMsgId` 空**=从会话最早起 **`count`** 条；**非空**=严格晚于该 id 的后 **`count`** 条；编码同 **`ListMessages`** |
| `MarkMessageRead` | markMessageRead | caller, messageId, readTimestampMs | `bool` | |
| `GetUnreadSessionMessageIds` | getUnreadSessionMessageIds | caller, imSessionId, limit | `bytes[]` | 每元素为 **16B `messageId`**。native 调用 **`MM2::GetUnreadSessionMessages`**；配对第二元当前为 **0**（未读占位），JNI 仅输出 id 列表即可（见 **`MM2.h`** / **`ZChatIM/docs/JNI-API-Documentation.md`** 第2.2节） |
| `StoreMessageReplyRelation` | storeMessageReplyRelation | caller, messageId, repliedMsgId, repliedSenderId, repliedContentDigest, senderId, signatureEd25519 | `bool` | 先 MM1 校验 |
| `GetMessageReplyRelation` | getMessageReplyRelation | caller, messageId | `bytes[]` | 三元组各为一行元素 |
| `EditMessage` | editMessage | caller, messageId, newEncryptedContent, editTimestampSeconds, signature, senderId | `bool` | |
| `GetMessageEditState` | getMessageEditState | caller, messageId | `bytes` | **12 字节大端**：editCount[4]+lastEditTime[8] |

## 三、用户数据模块

| C++ | 逻辑（camelCase） | 输入 | 输出（C++） | 说明 |
|-----|-------------------|------|-------------|------|
| `StoreUserData` | storeUserData | caller, userId, type(int32), data | `bool` | |
| `GetUserData` | getUserData | caller, userId, type(int32) | `bytes`（空=null） | |
| `DeleteUserData` | deleteUserData | caller, userId, type(int32) | `bool` | |

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
| `CreateGroup` | createGroup | caller, creatorId, name(string) | `bytes`（空=null） | groupId |
| `InviteMember` | inviteMember | caller, groupId, userId | `bool` | |
| `RemoveMember` | removeMember | caller, groupId, userId | `bool` | |
| `LeaveGroup` | leaveGroup | caller, groupId, userId | `bool` | |
| `GetGroupMembers` | getGroupMembers | caller, groupId | `bytes[]` | |
| `UpdateGroupKey` | updateGroupKey | caller, groupId | `bool` | |

## 六、群聊安全特性（Mention / Mute / GroupName）

| C++ | 逻辑（camelCase） | 输入 | 输出（C++） | 说明 |
|-----|-------------------|------|-------------|------|
| `ValidateMentionRequest` | validateMentionRequest | caller, groupId, senderId, mentionType(int32), mentionedUserIds, nowMs, signatureEd25519 | `bool` | |
| `RecordMentionAtAllUsage` | recordMentionAtAllUsage | caller, groupId, senderId, nowMs | `bool` | |
| `MuteMember` | muteMember | caller, groupId, userId, mutedBy, startTimeMs, durationSeconds(int64), reason | `bool` | |
| `IsMuted` | isMuted | caller, groupId, userId, nowMs | `bool` | MM1 内部可有剩余时长 out；JNI 仅 bool |
| `UnmuteMember` | unmuteMember | caller, groupId, userId, unmutedBy | `bool` | |
| `UpdateGroupName` | updateGroupName | caller, groupId, updaterId, newGroupName, nowMs | `bool` | MM1→MM2 串联 |
| `GetGroupName` | getGroupName | caller, groupId | `std::string` | MM2 查询 |

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
| `RegisterDeviceSession` | registerDeviceSession | caller, userId, deviceId, sessionId, loginTimeMs, lastActiveMs | `bytes`（空=无踢出） | **id 向量 16B**，见文首 ID 说明 |
| `UpdateLastActive` | updateLastActive | caller, userId, sessionId, nowMs | `bool` | |
| `GetDeviceSessions` | getDeviceSessions | caller, userId | `bytes[]` | 每元素 **48B 大端** |
| `CleanupExpiredDeviceSessions` | cleanupExpiredDeviceSessions | caller, nowMs | `void` | |
| `GetUserStatus` | getUserStatus | caller, userId | `bool` | online=true |
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
| `GenerateMasterKey` | generateMasterKey | caller | `bytes` | 与 KeyManagement 数据源统一 |
| `RefreshSessionKey` | refreshSessionKey | caller | `bytes` | `GetKeyManagement().RefreshSessionKey()` |
| `EmergencyWipe` | emergencyWipe | caller | `void` | 高危；须强授权 |
| `GetStatus` | getStatus | caller | `mapSS` | |
| `RotateKeys` | rotateKeys | caller | `bool` | |
| `ConfigurePinnedPublicKeyHashes` | configurePinnedPublicKeyHashes | caller, currentSpkiSha256, standbySpkiSha256 | `void` | |
| `VerifyPinnedServerCertificate` | verifyPinnedServerCertificate | caller, clientId, presentedSpkiSha256 | `bool` | caller 空见文首 TLS 说明 |
| `IsClientBanned` | isClientBanned | caller, clientId | `bool` | |
| `RecordFailure` | recordFailure | caller, clientId | `void` | caller 空见文首 TLS 说明 |
| `ClearBan` | clearBan | caller, clientId | `void` | |
| `DeleteAccount` | deleteAccount | caller, userId, reauthToken, secondConfirmToken | `bool` | |
| `IsAccountDeleted` | isAccountDeleted | caller, userId | `bool` | |
| `UpdateFriendNote` | updateFriendNote | caller, userId, friendId, newEncryptedNote, updateTimestampSeconds, signatureEd25519 | `bool` | |

## 十一、JNI 调用验证（C++ 重载）

| C++ | 逻辑（camelCase） | 输入 | 输出（C++） | 说明 |
|-----|-------------------|------|-------------|------|
| `ValidateJniCall()` | validateJniCall | - | `bool` | 退化路径；不得作为唯一安全边界 |
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
