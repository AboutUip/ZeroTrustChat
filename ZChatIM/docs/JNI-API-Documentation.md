# ZChatIM JNI API 契约文档（详细版）

说明：本文档面向 **`ZChatIM/include/jni/JniInterface.h`** 与 **`ZChatIM/include/jni/JniBridge.h`**（对外方法名、顺序、签名须一致：`JniInterface` 为 static 门面，`JniBridge` 为进程内单例桥接），作为 “Java → JNI → C++(MM1/MM2)” 触发入口契约的详细说明。文档以安全不变量为优先，重点写清楚每个 API 的职责、必须遵守的安全校验/落点路由、入参语义、返回语义、以及实现规划。

**概要表（含 `C++` 列严格对照）**：**`docs/06-Appendix/01-JNI.md`**。修改契约时须 **该表、本文、`JniInterface.h`、`JniBridge.h`** 四方同步。构建见 **`docs/07-Engineering/01-Build-ZChatIM.md`**。

**章节对应（严格）**：`01-JNI.md` 零〜十二节 ↔ 本文 第2.0节至第2.10节 及 第1节；其中「十、安全模块」拆为本文 第2.9节 中密钥/运维/证书/注销/好友备注等小节。

## 0. 总体边界与路由原则
本系统存在明确的信任边界：JNI/Java 属于不可信区，MM1/MM2 属于可信区。JniBridge 必须把每个 JNI 触发入口路由到对应的 MM1 或 MM2 管理器契约，不能在 JniBridge 中“绕过 MM1 签名/权限校验，直接调用 MM2 存储能力”。

职责分离的安全不变量如下。
1. 消息撤回/删除：必须通过 `mm1::MessageRecallManager` 完成签名与“仅发送者可撤回”的校验与销毁级别逻辑，不允许直接调用 `mm2::MM2::DeleteMessage`。
2. Mention：必须通过 `mm1::MentionPermissionManager::ValidateMentionRequest(..., signatureEd25519)` 完成签名与权限校验，只有校验通过后才能调用 `RecordMentionAtAllUsage`。
3. 好友验证：必须通过 `mm1::FriendManager` 完成 timestamp + Ed25519 signature 的校验与好友请求/响应/删除的状态流转，不允许在 JniBridge 里绕过到 MM2。
4. 回复关系落库：必须先走 `mm1::MessageReplyManager::StoreMessageReplyRelation` 完成发送者身份签名校验，再落到 `mm2::MM2::StoreMessageReplyRelation`。
5. 存储完整性（SQLite）：对 `.zdb` 的写入/读取必须触发 sha256 计算与 `BlockIndex`/SQLite 记录/比对链路，且读取比对失败必须采取明确策略（返回 false/拒绝解密/标记失效等），不能默默继续。
6. **callerSessionId**：除 `Initialize` / `Cleanup` / `Auth` / `VerifySession` / `ValidateJniCall` 两重载外，**所有** JNI 业务入口首参为 `callerSessionId`（`std::vector<uint8_t>`，长度 `Types::JNI_AUTH_SESSION_TOKEN_BYTES`，与 `Auth` 返回一致）。**`JniBridge`** 用 **`TryBindCaller` → `AuthSessionManager::TryGetSessionUserId`** 判定会话有效（与 **`VerifySession(caller)`** **等价**）。**principal 与后续 id 的绑定按 API 分流**，并非每个入口都要求 `userId==principal` 或 `imSessionId==principal`；**权威清单**：**`docs/06-Appendix/01-JNI.md`「principal 绑定矩阵」**（与 **`src/jni/JniBridge.cpp`** 一致）。**`DestroySession`** 为 `(callerSessionId, sessionIdToDestroy)`，两会话须解析为**同一** principal。细则见 **`JniSecurityPolicy.h`**。
7. **并发**：`JniBridge` 每个 public 方法入口 SHOULD 在 `m_apiRecursiveMutex` 下串行进入 MM1/MM2；`MM1` 的 **public 实例方法**（含各 `Get*Manager()`）入口已持 **`MM1::m_apiRecursiveMutex`**；**`MessageReplyManager::StoreMessageReplyRelation`** 入口再次持同一递归锁（与从 `MM1` 嵌套调用可重入）。`MM2::m_stateMutex` 为递归互斥；`MessageQueryManager` 仅在 MM2 已持有 `m_stateMutex` 时调用（禁止将查询器引用泄露到无同步路径）。**锁顺序**：须同时触及 MM1+MM2 时 **先 MM1（或 JniBridge 等价锁）再 MM2**，见 `include/common/JniSecurityPolicy.h` §7。

## 1. 命名与返回语义约定
1. 返回 `true`/`false`：true 表示该操作已成功完成；false 表示失败或安全校验未通过。
2. 返回 `std::vector<uint8_t>`：空 vector 语义为“null”（文档中明确的 null/空语义）。
3. 返回 `std::vector<std::vector<uint8_t>>`：空二维数组语义为“无结果/无条目”，不等价于失败（除非文档另行说明）。
4. 输出 bytes 的“bytes(payload)”语义：返回向量按实现约定的编码打包；本项目约定部分可在对应功能文档（如 MessageEdit）中查到。
5. 下文凡未单独写出 `callerSessionId` 的 API 小节，均须在实现中于**首参**传入并校验（与 `01-JNI.md` 表一致）；`imSessionId` 表示即时通讯会话通道 ID，区别于 ZSP 头 4 字节 `SESSION_ID_SIZE`。

## 2. API 详细说明（按接口分组）

### 2.0 生命周期（与 `docs/06-Appendix/01-JNI.md`「零、生命周期」一致）
#### `initialize(dataDir, indexDir) -> true/false` / `Initialize(const std::string&, const std::string&)`（C++）
职责：完成可信区与 JNI 桥初始化：**`MM1::Initialize`** + **`MM2::Initialize(dataDir, indexDir)`**（两路径须非空）。
安全注意：未 `Initialize` 成功前，其余 JNI 入口应拒绝或返回失败语义。
Java：`com.yhj.zchat.jni.ZChatIMNative.initialize(String, String)`；native 由 **`jni/JniNatives.cpp`** **`RegisterNatives`** 绑定。

#### `cleanup() -> void` / `Cleanup()`（C++）
职责：与 `initialize` 对称释放资源。

### 2.1 认证模块
#### `auth(userId, token, clientIp?) -> sessionId/null`
职责：对用户进行身份认证并建立会话。
安全注意：限流/封禁由 `mm1::AuthSessionManager` 按 `docs/03-Business/02-Auth.md` 实现；JniBridge 不应重复实现。`clientIp` 为空时仅**用户级** 10 次/分钟；**非空**（建议 IPv4 4 字节 / IPv6 16 字节）时额外启用 **IP 级** 5 次/分钟，且封禁索引为 `userId‖clientIp`。
入参：`userId`、`token`、**`clientIp`（`bytes`，可空向量）**。
返回：`sessionId`（成功）或空向量（失败/无会话/限流/封禁）。
路由实现规划：`m_mm1.GetAuthSessionManager().Auth(userId, token, clientIp)`（C++ 默认参数可省略第三参，生产环境建议从连接上下文填入 IP）。

#### `verifySession(sessionId) -> active/invalid`
职责：检查会话是否仍有效。
安全注意：必须使用 MM1 的会话状态判断，不应在 JniBridge 侧做缓存绕过。
入参：`sessionId`。
返回：true 表示 active，有效；false 表示 invalid/不存在。
路由实现规划：调用 `m_mm1.GetAuthSessionManager().VerifySession(...)` 或会话管理器等价契约。

#### `destroySession(callerSessionId, sessionIdToDestroy) -> true/false`
职责：在已认证操作者上下文中销毁目标会话并清除 MM1 内存态（Level2 行为由实现决定）。
安全注意：实现 MUST 校验 `callerSessionId` 有效，且仅允许销毁与 principal 绑定的 `sessionIdToDestroy`（或具备等价运维授权）。
入参：`callerSessionId`、`sessionIdToDestroy`。
返回：true 成功，false 失败。
路由实现规划：调用 `m_mm1.GetAuthSessionManager().DestroySession(...)`（或带授权检查的包装）。

### 2.2 消息模块
#### `storeMessage(callerSessionId, imSessionId, payload) -> msgId/null`
职责：存储一条消息密文到 MM2。
安全注意：须先 **`TryBindCaller`**（有效 caller）；**`imSessionId`** 为聊天会话通道（**16B**，非 ZSP 头 4 字节）；**当前 `JniBridge` 不**校验 **`imSessionId == principal`**（见 **`01-JNI.md` 绑定矩阵**）；不进行撤回/签名校验（这些属于 Recall/Delete 或 Edit 等能力域）。
入参：`callerSessionId`、`imSessionId`、`payload`。
返回：`msgId` 成功返回，空 vector 表示失败。
路由实现规划：JniBridge 调用 `mm2::MM2::StoreMessage(...)`。

#### `retrieveMessage(callerSessionId, messageId) -> data/null`
职责：根据消息 ID 检索消息内容（密文或封装数据）。
安全注意：须先 **`TryBindCaller`**；**当前 `JniBridge` 不**按 **`messageId`/会话**做 **principal 可见性**校验（任何有效 caller 若能猜到 id 即可触发 MM2 读取；产品层须自限或未来在桥接层收紧）。
入参：`callerSessionId`、`messageId`。
返回：data 或空向量。
路由实现规划：调用 `mm2::MM2::RetrieveMessage(...)`。

#### `deleteMessage(callerSessionId, messageId, senderId, signatureEd25519) -> true/false`
职责：安全删除/撤回等价能力入口（契约语义对齐 MessageRecall）。
安全注意：必须走 `mm1::MessageRecallManager` 完成签名与“仅发送者可撤回”的校验与 Level2 覆写。
入参：`callerSessionId`、`messageId`、`senderId`、`signatureEd25519`。
返回：true 成功，false 失败/校验未通过。
路由实现规划：调用 `m_mm1.GetMessageRecallManager().DeleteMessage(...)`（或等价 Recall 语义）。
禁止项：禁止直接调用 `mm2::MM2::DeleteMessage(msgId)`。

#### `recallMessage(callerSessionId, messageId, senderId, signatureEd25519) -> true/false`
职责：安全撤回消息（Level2 覆写与索引状态更新）。
安全注意：同 deleteMessage 的签名校验与发送者权限校验。
入参：`callerSessionId`、`messageId`、`senderId`、`signatureEd25519`。
返回：true 成功，false 失败。
路由实现规划：调用 `m_mm1.GetMessageRecallManager().RecallMessage(...)`。

#### `listMessages(callerSessionId, userId, count) -> array`
职责：返回最近消息列表（按实现约定返回每条消息的 payload 封装）。
安全注意：须校验 `userId` 与 principal 的授权关系（或管理员读权限）；查询仍走 MM2。
入参：`callerSessionId`、`userId`、`count`。
返回：二维数组（每条一条 payload）。
路由实现规划：查询应走 `mm2::MM2::GetMessageQueryManager()`（`MessageQueryManager` 为 MM2 成员，与索引/.zdb 同生命周期）；不得在 JniBridge 侧单独 new 查询器。

#### `listMessagesSinceTimestamp(callerSessionId, userId, sinceTimestampMs, count) -> array`
职责：消息同步按时间戳游标拉取。
入参：`callerSessionId`、`userId`、`sinceTimestampMs`（毫秒 epoch）、`count`。
返回：二维数组。
路由实现规划：走 `mm2::MM2::GetMessageQueryManager().ListMessagesSinceTimestamp(...)`。

#### `listMessagesSinceMessageId(callerSessionId, userId, lastMsgId, count) -> array`
职责：消息同步按最后消息 ID 游标拉取。
入参：`callerSessionId`、`userId`、`lastMsgId`、`count`。
返回：二维数组。
路由实现规划：走 `mm2::MM2::GetMessageQueryManager().ListMessagesSinceMessageId(...)`。

#### `markMessageRead(callerSessionId, messageId, readTimestampMs) -> true/false`
职责：把消息标记为已读，影响未读 LRU 与读取状态。
安全注意：**当前 `JniBridge` 仅** **`TryBindCaller`**，**不**校验消息归属或 **`imSessionId`**（见 **`01-JNI.md` 绑定矩阵**）。
入参：`callerSessionId`、`messageId`、`readTimestampMs`。
返回：true 成功，false 失败。
路由实现规划：调用 `mm2::MM2::MarkMessageRead(...)`。

#### `getUnreadSessionMessageIds(callerSessionId, imSessionId, limit) -> array<messageId>`
职责：获取会话未读消息 ID 列表（供客户端展示未读队列或驱动同步）。
安全注意：**当前 `JniBridge` 仅** **`TryBindCaller` + `imSessionId` 长度**；**不**校验 **`imSessionId == principal`**（见 **`01-JNI.md` 绑定矩阵**）。
入参：`callerSessionId`、`imSessionId`、`limit`。
返回：消息 ID 列表（二维数组，每个元素为 **16B `messageId`**）。
路由实现规划：调用 `mm2::MM2::GetUnreadSessionMessages(sessionId, limit, outPairs)` 后，**仅将 `messageId` 压入 JNI 返回值**（**`outPairs[i].second`** 当前恒为 **0**，占位）。**MM2 已实现**；**`JniBridge` → `jni/JniNatives.cpp` → `ZChatIMNative.getUnreadSessionMessageIds`** 已接线。

#### `storeMessageReplyRelation(callerSessionId, senderEd25519PublicKey, messageId, repliedMsgId, repliedSenderId, repliedContentDigest, senderId, signatureEd25519) -> true/false`
职责：存储“回复关系”链路（reply TLV 0x10 对应）。
安全注意：**`JniBridge` 本方法不调用 `TryBindCaller`**；**`MessageReplyManager::StoreMessageReplyRelation`** 内以 **`TryGetSessionUserId(callerSessionId)`** 取 principal，**必须**与 **`senderId`**（16B）一致；**`signatureEd25519`**（64B 分离签名）须在 **canonical payload** 上通过 **`senderEd25519PublicKey`（32B）** 验证。  
**Native 验签后端**：**全平台** OpenSSL 3 **EVP_PKEY_ED25519**；与标准 **RFC 8032** 分离签名互操作。  
**Canonical payload（定长拼接，无额外分隔符）**：ASCII **`ZChatIM|StoreMessageReplyRelation|v1`** ‖ **messageId(16)** ‖ **repliedMsgId(16)** ‖ **repliedSenderId(16)** ‖ **repliedContentDigest(32)** ‖ **senderId(16)**。Java 侧签名须与此字节序列**完全一致**。
入参：`callerSessionId` + **`senderEd25519PublicKey`** + reply 四元组 + **`senderId`** + **`signatureEd25519`**。
返回：true 成功，false 失败。
路由实现规划：JniBridge 调用 `mm1::MessageReplyManager::StoreMessageReplyRelation(...)`，管理器内部再调用 MM2 落库契约。
禁止项：禁止在 JniBridge 直接调用 `mm2::MM2::StoreMessageReplyRelation(...)`（那会绕过签名校验）。

#### `getMessageReplyRelation(callerSessionId, messageId) -> array{repliedMsgId,repliedSenderId,repliedContentDigest}`
职责：读取回复关系摘要（例如用于消息 UI 展示“被回复对象摘要”）。
安全注意：只读，不要求签名输入；**当前 `JniBridge` 仅** **`TryBindCaller`**，**不**做消息级 principal 可见性校验（见 **`01-JNI.md` 绑定矩阵**）。
入参：`callerSessionId`、`messageId`。
返回：回复关系摘要数组。
路由实现规划：调用 `mm2::MM2::GetMessageReplyRelation(...)`。

#### `editMessage(callerSessionId, messageId, newEncryptedContent, editTimestampSeconds, signature, senderId) -> true/false`
职责：编辑消息密文与更新编辑状态。
安全注意：编辑权限校验由 MM1 管理器完成（时间窗、editCount 上限、签名验证、发送者身份一致等）。
入参：`callerSessionId`、`messageId`、`newEncryptedContent`、`editTimestampSeconds`、`signature`、`senderId`。
返回：true 成功，false 失败。
路由实现规划：调用 `m_mm1.GetMessageEditOrchestration().EditMessage(...)` 或对等校验入口。

#### `getMessageEditState(callerSessionId, messageId) -> bytes(payload)`
职责：返回编辑状态（editCount 与 lastEditTimeSeconds 打包）。
安全注意：**当前 `JniBridge` 仅** **`TryBindCaller`**（见 **`01-JNI.md` 绑定矩阵**）。
入参：`callerSessionId`、`messageId`。
返回：bytes（实现约定为 12 bytes：4 + 8）。
路由实现规划：调用 `mm1::MessageEditManager::GetEditState` 或 MM1 组合契约。

### 2.3 用户数据模块
#### `storeUserData(callerSessionId, userId, type, data) -> true/false`
职责：存储用户元数据（由 type 决定语义）。
安全注意：输入 data 由上层加密/封装后传入；MM1/MM2 只按类型存取；须绑定 `userId` 与 principal。
入参：`callerSessionId`、`userId`、`type`、`data`。
返回：true 成功，false 失败。
**已实现**：`JniBridge::StoreUserData` → `mm1::UserDataManager::StoreUserData` → **`MM2::StoreMm1UserDataBlob`**（**MM2 已 Initialize** 时写入元库表 **`mm1_user_kv`**，见 **`docs/02-Core/03-Storage.md`** v5）；**MM2 未初始化**时回退进程内内存表。

#### `getUserData(callerSessionId, userId, type) -> data/null`
职责：查询用户元数据。
入参：`callerSessionId`、`userId`、`type`。
返回：data 或空向量（无键、空 BLOB、无效 caller、principal 不匹配时均为空）。
**已实现**：`UserDataManager::GetUserData` → **`MM2::GetMm1UserDataBlob`**（或内存回退）。

#### `deleteUserData(callerSessionId, userId, type) -> true/false`
职责：删除用户元数据条目。
入参：`callerSessionId`、`userId`、`type`。
返回：true 成功删除至少一行，false 无匹配行或其它失败。
**已实现**：`UserDataManager::DeleteUserData` → **`MM2::DeleteMm1UserDataBlob`**（或内存回退）。

### 2.4 好友模块
#### `sendFriendRequest(callerSessionId, fromUserId, toUserId, timestampSeconds, signatureEd25519) -> requestId/null`
职责：发起好友请求并生成 requestId。
安全注意：必须校验 sender 身份 signatureEd25519（文档 `FriendVerify`）；`fromUserId` MUST 与 principal 一致。
入参：`callerSessionId`、from/to 用户、timestamp、signature。
返回：requestId 或空向量。
**已实现**：`JniBridge` → **`FriendManager::SendFriendRequest`**（**`FriendVerificationManager`** 验签 → **`MM2::StoreFriendRequest`**）。**canonical**：见 **`docs/04-Features/05-FriendVerify.md` 附录 A**。

#### `respondFriendRequest(callerSessionId, requestId, accept, responderId, timestampSeconds, signatureEd25519) -> true/false`
职责：响应好友请求，同步状态流转（pending -> accepted/rejected）。
安全注意：必须校验响应者签名并确保 requestId 与 responderId 的一致性。
入参：`callerSessionId`、requestId、accept、responderId、timestamp、signature。
返回：true 成功，false 失败。
**已实现**：须 **`responderId == to_user`** 且请求为 **pending**；验签后 **`MM2::UpdateFriendRequestStatus`**。

#### `deleteFriend(callerSessionId, userId, friendId, timestampSeconds, signatureEd25519) -> true/false`
职责：删除好友关系（标记删除/可恢复语义由实现决定）。
安全注意：必须校验删除操作的签名与双向身份一致性。
入参：`callerSessionId`、userId、friendId、timestamp、signature。
返回：true 成功，false 失败。
**已实现**：验签后删除 **accepted** 边（**`MM2` / SQLite**）。

#### `getFriends(callerSessionId, userId) -> array`
职责：返回好友列表（可包含状态字段，具体由实现约定）。
入参：`callerSessionId`、userId。
返回：好友列表数组。
**已实现**：列出 **`friend_requests`** **accepted** 对端 **user_id**。

### 2.5 群组模块与群聊安全特性（Mention/Mute/GroupName）
#### 群基础管理
`createGroup`、`inviteMember`、`removeMember`、`leaveGroup`、`getGroupMembers`、`updateGroupKey`（**均以 `callerSessionId` 为首参**，详见 `docs/06-Appendix/01-JNI.md`）：
职责：群的创建、成员变更、以及群密钥信封 **`ZGK1`** 的轮换（**已实现**落 **`group_data` + `.zdb`**，非占位）。
安全注意：**`JniBridge`** 对 **`createGroup`/`leaveGroup`** 做 **`principal` 与 `creatorId`/`userId` 的 `ConstantTimeCompare`**；**`inviteMember`/`removeMember`/`getGroupMembers`/`updateGroupKey`** **不**将 `principal` 与参数里的被操作 **userId** 做相等比对，但 **`mm1::GroupManager`** 将 **会话解析出的 `principal`** 作为 **邀请者/踢人者/列表查看者/密钥轮换发起者**，并在 **SQLite `group_members`** 上校验 **role**（**0=member，1=admin，2=owner**；邀请须 admin/owner，踢人须 owner，见 **`GroupManager.h`**）。**`inviteMember`** 另要求 **`ListAcceptedFriendPeerUserIds(principal)`** 含被邀请 **userId**（与源码一致；正常 **status=1** 数据下与双向好友等价）。**`MM2::TryGetGroupKeyEnvelopeForMm1` / `SeedAcceptedFriendshipForSelfTest`**：**不得**经 JNI 暴露（见 **`JniSecurityPolicy.h`**）。
入参与返回：**`createGroup` 的群名** UTF-8 **≤2048 字节**；失败时 **`createGroup`** 返回空向量、其余返回 false/空集。
**已实现**：**`JniBridge` → `GroupManager` → `MM2::*ForMm1`**（**`group_members`**、**`mm2_group_display`**、**`group_data` + `.zdb`** 的 **`ZGK1`** 群密钥信封；**`UpdateGroupKey`** 由 **owner/admin** 触发轮换）。

#### Mention
`validateMentionRequest(callerSessionId, groupId, senderId, mentionType, mentionedUserIds, nowMs, signatureEd25519) -> true/false`
职责：校验 @ 触发权限与成员合法性，并执行 @ALL 频率限制前置判断。
安全注意：必须用 `signatureEd25519` 校验发送者身份；返回 true 才能执行下一步记录。
路由实现规划：调用 `mm1::MentionPermissionManager::ValidateMentionRequest(...)`。

`recordMentionAtAllUsage(callerSessionId, groupId, senderId, nowMs) -> true/false`
职责：记录一次 @ALL 使用次数，用于速率限制。
安全注意：只能在 validateMentionRequest 返回 true 后调用，且 nowMs 必须与校验时的 nowMs 语义一致。
路由实现规划：调用 `mm1::MentionPermissionManager::RecordMentionAtAllUsage(...)`。

#### GroupMute
`muteMember(callerSessionId, groupId, userId, mutedBy, startTimeMs, durationSeconds, reason) -> true/false`
`isMuted(callerSessionId, groupId, userId, nowMs) -> true/false`
`unmuteMember(callerSessionId, groupId, userId, unmutedBy) -> true/false`
职责：禁言、查询禁言状态、解禁。
安全注意：**`mutedBy` / `unmutedBy`** 须为群内 **owner/admin**；**admin** 仅可禁言 **member**；**不可**禁 **owner**。**`IsMuted`** 仅校验 **caller**。
路由：**`JniBridge` → `mm1::GroupMuteManager`** → **`MM2` / `mm2_group_mute`**（**`03-Storage.md`**）。

#### GroupName
`updateGroupName(callerSessionId, groupId, updaterId, newGroupName, nowMs) -> true/false`
`getGroupName(callerSessionId, groupId) -> string`
职责：群名称更新与查询。
安全注意：**`updaterId` 须为群内 owner/admin**；**`newGroupName`** 非空、**UTF-8 ≤2048 字节**。**频率限制 / 敏感词**等可由上层补充；**`nowMs`** 用于写入 **`updated_s`**（**`nowMs/1000`** 截断为秒）。
路由：**update** → **`mm1::GroupNameManager`** → **`MM2::UpdateGroupName`**；**get** → **`MM2::GetGroupName`**（绑定矩阵见 **`01-JNI.md`**）。

### 2.6 文件模块（含续传与 SQLite 完整性校验）
#### 基础文件分片
`storeFileChunk(callerSessionId, fileId, chunkIndex, data) -> true/false`
职责：将文件分片写入 `.zdb`（或与实现等价存储），支持续传。
安全注意：必须触发 sha256 完整性链路：写入后计算 sha256，并调用 `RecordDataBlockHash` 记录到 SQLite。
路由实现规划：JniBridge 调 `mm2::MM2::StoreFileChunk(...)`，MM2 的实现应内部串联 `StorageIntegrityManager`。

`getFileChunk(callerSessionId, fileId, chunkIndex) -> data/null`
职责：读取文件分片。
安全注意：读取后计算 sha256，并调用 `VerifyDataBlockHash` 与 SQLite 比对；不一致必须标记失效并返回失败。
路由实现规划：调 `mm2::MM2::GetFileChunk(...)`。

`completeFile(callerSessionId, fileId, sha256) -> true/false`
职责：文件传输完成，校验完整文件 sha256。
安全注意：完整文件 sha256 与分片 hash 记录不一致时返回 false 并进入失败策略。
路由实现规划：调 `mm2::MM2::CompleteFile(...)`。

`cancelFile(callerSessionId, fileId) -> true/false`
职责：取消传输并清理传输状态。
路由实现规划：调 `mm2::MM2::CancelFile(...)`。

#### 续传断点（内存）
`storeTransferResumeChunkIndex(callerSessionId, fileId, chunkIndex) -> true/false`
`getTransferResumeChunkIndex(callerSessionId, fileId) -> chunkIndex(uint32)`
`cleanupTransferResumeChunkIndex(callerSessionId, fileId) -> true/false`
职责：记录接收进度断点（仅内存）。
安全注意：服务重启断点丢失属于已知约束；不要假设断点一定可用。
路由实现规划：调 `mm2::MM2` 对应续传断点契约。

### 2.7 会话与多设备
会话与多设备方法的入参/返回语义遵循 `01-JNI.md` 表格（**首参均为 `callerSessionId`**，会话类参数为 `imSessionId` 或设备表 `sessionId` 见表）。**`getSessionMessages` / `getSessionStatus` / `touchSession` / `cleanupExpiredSessions` / `cleanupSessionMessages`**：当前 **`JniBridge` 仅** **`TryBindCaller` + `imSessionId` 长度（如适用）**，**不**强制 **`imSessionId == principal`**（见 **`01-JNI.md` principal 绑定矩阵**）。

`getSessionMessages(callerSessionId, imSessionId, limit)`：读取会话消息，走 MM2。
`getSessionStatus(callerSessionId, imSessionId)`：会话 active/invalid，走 MM1 `SessionActivityManager`。
`touchSession(callerSessionId, imSessionId, nowMs)`：心跳保活更新 lastActive，走 MM1。
`cleanupExpiredSessions(callerSessionId, nowMs)`：清理超时会话，走 MM1（定时任务须持有效运维/系统会话，由实现定义）。
`registerDeviceSession(callerSessionId, userId, deviceId, sessionId, loginTimeMs, lastActiveMs)`：最多 2 设备；踢最早设备，走 MM1 `DeviceSessionManager`。C++：**`bool` + `outKickedSessionId`**。Java：**`null`**=失败；**`byte[0]`**=成功且无踢出；**长度 16**=被踢出的设备会话 id。
`updateLastActive`、`getDeviceSessions`、`cleanupExpiredDeviceSessions`：同理，首参 `callerSessionId`，走 MM1。
`getUserStatus(callerSessionId, userId)`：在线/离线，走 MM1 `UserStatusManager`。
`cleanupSessionMessages(callerSessionId, imSessionId)`：清理会话过期消息，走 MM2。

### 2.8 数据清理与统计
`cleanupExpiredData(callerSessionId)`：清理过期数据（MM1/MM2 各自实现范围）。
`optimizeStorage(callerSessionId)`：存储优化（实现策略由实现决定）。
`getStorageStatus(callerSessionId)`：返回存储状态键值对。
`getMessageCount(callerSessionId)`：消息数量统计。
`getFileCount(callerSessionId)`：文件数量统计。

安全注意：清理与统计属于运维能力，必须遵循“可信区操作”原则，且不泄露敏感内容（实现方决定返回粒度）。

### 2.9 安全运维/密钥/证书固定/注销/好友备注
#### 密钥与运维
`generateMasterKey(callerSessionId)`：密钥管理，走 `MM1::GenerateMasterKey()` 或 `MM1::GetKeyManagement().GenerateMasterKey()`（实现须统一数据源，避免两套主密钥状态）。

`refreshSessionKey(callerSessionId)`：走 **`MM1::GetKeyManagement().RefreshSessionKey()`**（`MM1` 顶层无同名方法，不得凭空实现）。
`emergencyWipe(callerSessionId)`：紧急销毁，走 MM1 `SystemControl`（须强授权）。
`getStatus(callerSessionId)`：系统状态，走 MM1。
`rotateKeys(callerSessionId)`：密钥轮换，走 MM1。

#### 证书固定（SPKI SHA-256 Pin）
`configurePinnedPublicKeyHashes(callerSessionId, currentSpkiSha256, standbySpkiSha256)`
`verifyPinnedServerCertificate(callerSessionId, clientId, presentedSpkiSha256)`（TLS 回调链上 `callerSessionId` 可为空须满足 `JniSecurityPolicy` 条件）
`isClientBanned(callerSessionId, clientId)`
`recordFailure(callerSessionId, clientId)`（同上 empty-caller 规则）
`clearBan(callerSessionId, clientId)`
职责：TLS 服务器证书公钥哈希固定与连续失败封禁管理。
安全注意：失败计数与封禁策略由 MM1 `CertPinningManager` 管理；JniBridge 不应实现复制逻辑。

#### 账户注销
`deleteAccount(callerSessionId, userId, reauthToken, secondConfirmToken)`
`isAccountDeleted(callerSessionId, userId)`
职责：注销触发 Level3 销毁与注销状态查询。
安全注意：reAuth 与 secondConfirm 必须由实现完成有效性校验（契约上由入参承载）。

#### 好友备注
`updateFriendNote(callerSessionId, userId, friendId, newEncryptedNote, updateTimestampSeconds, signatureEd25519)`
职责：更新好友备注。
安全注意：签名与身份校验由 MM1 `FriendNoteManager` 或等价校验入口完成。

### 2.10 JNI 调用验证
`validateJniCall() -> true/false`
职责：验证 JNI 调用合法性（例如检查初始化状态、JNI 环境/类是否正确等）。
安全注意：由于本项目暂时移除 JNI 依赖，validate 的强度取决于未来 JNI 适配层传入 env/class 的实现方式。实现方不得仅返回固定值。

`validateJniCall(jniEnv, jclass) -> true/false`（`JniInterface`/`JniBridge` 以 `void*` 承载指针，避免头文件依赖 `jni.h`）
职责：**优先在 JNI 入口使用**：将 env/class 转交 `mm1::MM1::ValidateJniCall(env, class)`（或等价 `JniSecurity` 校验）。
无参版本仅可作为退化路径（如非 JNI 调用方），不得作为唯一安全边界。

## 3. 规划的实现（实现闭环的建议步骤）
本节给出可执行的实现规划，用来保证“接口闭环 + 安全不变量 + StorageIntegrity + SQLite 校验闭环”同时满足。

### 3.1 阶段一：路由与安全落点固定
1. 在 `JniBridge` 每个 JNI 方法里：先 `std::lock_guard` 持 `m_apiRecursiveMutex`；对非 `Initialize`/`Cleanup`/`Auth`/`VerifySession`/`ValidateJniCall*` 入口，先校验 `callerSessionId`（`VerifySession` + principal 绑定），再明确调用链路进入 MM1 manager 完成校验，最后进入 MM2 存储。
2. 对每个“禁止项”在**安全评审与联调**中验证（例如直接调用 MM2 删除是否会绕过签名）。
3. 对 `RecallMessage/DeleteMessage`、`Mention`、`FriendVerify`、`StoreMessageReplyRelation` 等路径验证：**签名错误时必须失败**。

### 3.2 阶段二：StorageIntegrity + SQLite 完整性校验闭环
1. 实现 `StorageIntegrityManager::ComputeSha256/RecordDataBlockHash/VerifyDataBlockHash`（底层已落至 **`mm2::SqliteMetadataDb`**：`UpsertDataBlock` / `GetDataBlock`；详见仓库根目录 **`docs/02-Core/03-Storage.md`** 第七节）。
2. 在 `MM2::StoreFileChunk` 写入成功后调用 `RecordDataBlockHash`（需先 `Bind` 元数据库并完成 `UpsertZdbFile` 等外键前提）。
3. 在 `MM2::GetFileChunk` 读取成功后调用 `VerifyDataBlockHash`，并对 outMatch=false 的策略进行明确化（返回失败或标记失效）。
4. **`BlockIndex` 规划**与 **`SqliteMetadataDb` 现状**：表结构、事务与 `dataId`+`chunk_idx` 的查询/校验应以 **`03-Storage.md` 第二节、第2.6节** 及 **`SqliteMetadataDb`** 为准；`BlockIndex` 接入时应委托或复用该层，避免重复/schema 漂移。

### 3.3 阶段三：并发与事务一致性
1. 保证 `.zdb` 的文件锁策略与 SQLite 事务策略一致，避免写入 .zdb 成功但 SQLite 记录失败造成状态不一致。
2. 对 **BEGIN EXCLUSIVE** 等事务模式在多线程负载下进行**集成验证**。

### 3.4 阶段四：消息特性联调验收
1. 消息撤回/删除：Level2 覆写后与客户端展示策略一致（本地标记撤回等）。
2. 编辑：**editCount** 上限与编辑窗口行为符合功能规范。
3. 回复链路：删除/撤回后回复关系摘要仍可读。

### 3.5 阶段五：文档与契约一致性验证
1. 每次修改 `.h` 契约后，必须同步更新 `docs/06-Appendix/01-JNI.md`。
2. 使用脚本验证“docs 表项 -> JniInterface/JniBridge 方法集合”一致性（已在前序步骤完成）。

## 4. 交付物清单（验证是否“闭环且安全”）
当你实现完成后，建议以以下检查点作为验收准则。
1. 所有安全关键 JNI 方法都能观测到调用链进入 MM1 manager（可通过日志或断点追踪验证）。
2. `StorageIntegrityManager` 的 record/verify 在文件写读路径中都被调用且行为正确。
3. SQLite 表与字段创建成功，且能处理缺失记录、sha256 不一致等异常场景。
4. 典型错误输入下（签名错误、session 失效、@ALL 频率超限、文件 sha256 不一致）全部返回 false 或按设计拒绝。


