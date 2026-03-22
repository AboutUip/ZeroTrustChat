# ZChatIM JNI API 契约文档（详细版）

说明：本文档面向 **`ZChatIM/include/jni/JniInterface.h`** 与 **`ZChatIM/include/jni/JniBridge.h`**（对外方法名、顺序、签名须一致：`JniInterface` 为 static 门面，`JniBridge` 为进程内单例桥接），作为 “Java → JNI → C++(MM1/MM2)” 触发入口契约的详细说明。文档以安全不变量为优先，重点写清楚每个 API 的职责、必须遵守的安全校验/落点路由、入参语义、返回语义、以及实现规划。

**概要表（含 `C++` 列严格对照）**：**`docs/06-Appendix/01-JNI.md`**。修改契约时须 **该表、本文、`JniInterface.h`、`JniBridge.h`** 四方同步。构建见 **`docs/07-Engineering/01-Build-ZChatIM.md`**。

**章节对应（严格）**：`01-JNI.md` 零〜十二节 ↔ 本文 第2.0节至第2.10节 及 第1节；其中「十、安全模块」拆为本文 第2.9节 中密钥/运维/证书/注销/好友备注等小节。

**常见 IM 场景怎么调 API**：见本文 **§0.5～§0.5.3**（含 **§0.5.1～0.5.2** 边界）；**逐方法契约与 principal 矩阵**仍以 **`01-JNI.md`** 与 **§2** 为准。

## 0. 总体边界与路由原则
本系统存在明确的信任边界：JNI/Java 属于不可信区，MM1/MM2 属于可信区。JniBridge 必须把每个 JNI 触发入口路由到对应的 MM1 或 MM2 管理器契约，不能在 JniBridge 中“绕过 MM1 签名/权限校验，直接调用 MM2 存储能力”。

职责分离的安全不变量如下。
1. 消息撤回/删除：必须通过 `mm1::MessageRecallManager` 完成签名与“仅发送者可撤回”的校验与销毁级别逻辑，不允许直接调用 `mm2::MM2::DeleteMessage`。
2. Mention：必须通过 `mm1::MentionPermissionManager::ValidateMentionRequest(..., signatureEd25519)` 完成签名与权限校验，只有校验通过后才能调用 `RecordMentionAtAllUsage`。
3. 好友验证：必须通过 `mm1::FriendManager` 完成 timestamp + Ed25519 signature 的校验与好友请求/响应/删除的状态流转，不允许在 JniBridge 里绕过到 MM2。
4. 回复关系落库：必须先走 `mm1::MessageReplyManager::StoreMessageReplyRelation` 完成发送者身份签名校验，再落到 `mm2::MM2::StoreMessageReplyRelation`。
5. 存储完整性（SQLite）：对 `.zdb` 的写入/读取必须触发 sha256 计算与 `BlockIndex`/SQLite 记录/比对链路，且读取比对失败必须采取明确策略（返回 false/拒绝解密/标记失效等），不能默默继续。
6. **callerSessionId**：除 `Initialize` / **`InitializeWithPassphrase`** / `Cleanup` / `Auth` / `VerifySession` / `ValidateJniCall` 两重载外，**所有** JNI 业务入口首参为 `callerSessionId`（`std::vector<uint8_t>`，长度 `Types::JNI_AUTH_SESSION_TOKEN_BYTES`，与 `Auth` 返回一致）。**`JniBridge`** 用 **`TryBindCaller` → `AuthSessionManager::TryGetSessionUserId`** 判定会话有效。**桥接已 `Initialize` 时**，与 **`VerifySession(caller)`** 对同一会话 id **结论等价**；未初始化时二者均失败短路。**principal 与后续 id 的绑定按 API 分流**，并非每个入口都要求 `userId==principal` 或 `imSessionId==principal`；**权威清单**：**`docs/06-Appendix/01-JNI.md`「principal 绑定矩阵」**（与 **`src/jni/JniBridge.cpp`** 一致）。**`DestroySession`** 为 `(callerSessionId, sessionIdToDestroy)`，两会话须解析为**同一** principal。细则见 **`JniSecurityPolicy.h`**。
7. **并发**：`JniBridge` 每个 public 方法入口 SHOULD 在 **`m_apiRecursiveMutex`** 下串行进入 MM1/MM2（**整段 JNI 调用**受桥接锁保护）。**`JniBridge::m_initialized`** 为 **`std::atomic<bool>`**：无锁查询（如内部 **`CheckInitialized`**）用 **acquire load**，与 **`Initialize`/`Cleanup`/`EmergencyWipe`** 的 **release store** 配对（见 **`JniSecurityPolicy.h` §5**）。**`MM1::Get*Manager()`**：仅在**取引用**的瞬间持 **`MM1::m_apiRecursiveMutex`**，返回后锁即释放；**随后**对 Manager 成员函数的调用**不一定**仍在 MM1 锁内——须依赖 **桥接锁**、**各 Manager 自有互斥**（如 **`UserDataManager::Impl::mutex`**）或 **Manager 入口自锁**（如 **`MessageReplyManager`/`MentionPermissionManager`** 对 **`MM1::m_apiRecursiveMutex`**）。**`MM2::m_stateMutex`** 为递归互斥；**`MM2::IsInitialized()`** 在锁内读 **`m_initialized`**。**`MessageQueryManager`** 仅在 MM2 已持有 **`m_stateMutex`** 时使用。**锁顺序**：须同时触及 MM1+MM2 时 **先 MM1（或 JniBridge 等价锁）再 MM2**，见 **`JniSecurityPolicy.h` 第7条**。

## 0.5 常见 IM 操作与 JNI 映射（产品视角）

本节回答「做一款常见 IM 客户端时，**大概要调哪些 native 方法**」。表中 **Java/camelCase** 与 **`ZChatIMNative`** / **`JniNatives.cpp`** 一致；**`callerSessionId`** 除单独说明外，业务调用均须传入 **`auth` 返回的会话句柄**（长度见 **`Types::JNI_AUTH_SESSION_TOKEN_BYTES`**）。**`imSessionId`** 为 **16B 会话通道 ID**（可与当前用户 **`principal` 不同**，见 **`01-JNI.md` 绑定矩阵**）。

| 常见产品能力 | 建议调用的 JNI（顺序/组合因 UI 而定） | 说明 |
|-------------|--------------------------------------|------|
| **冷启动 / 解锁本地库** | `initialize` 或 **`initializeWithPassphrase`** → `getStatus` | 须先于业务；口令路径见 **§2.0**、**`05`**。 |
| **登录（建立 native 会话）** | `auth(userId, token, clientIp?)` → 保存返回的 **sessionId** 作为后续 **`callerSessionId`** | **非**「用户注册」；凭证由**上层/服务端**签发。 |
| **会话是否仍有效** | `verifySession(sessionId)` | 与桥接已初始化时 **`caller`** 校验结论对齐见 **§0 第6条**。 |
| **登出 / 踢掉本会话** | `destroySession(caller, sessionIdToDestroy)` | 同人双会话互毁；见 **`01-JNI.md`**。 |
| **进程退出 / 对称释放 native** | `cleanup` | 与 **`initialize`** 对称；进程结束或切换数据根目前建议成对调用（**§2.0** / **`01-JNI.md` 零节**）。 |
| **发文本/密文消息** | `storeMessage(caller, imSessionId, payload)` | 返回 **16B `messageId`**；**`principal`** 作为发送者写入 RAM 行，供编辑/撤回绑定。 |
| **按 id 拉一条** | `retrieveMessage(caller, messageId)` | |
| **会话内最近消息（按 imSessionId）** | `getSessionMessages(caller, imSessionId, limit)` | 聊天页常用；**不**强制 **`imSessionId == principal`**（绑定矩阵）。 |
| **按用户维度最近 N 条** | `listMessages(caller, userId, count)` | 返回 **`message_id(16)‖payload_len(u32 BE)‖payload`**；**`userId` 须与 principal 一致**。 |
| **增量同步（时间戳 / 游标）** | `listMessagesSinceTimestamp` / **`listMessagesSinceMessageId`** | 适合「从某刻/某条之后拉一页」；编码同 **`01-JNI.md` 二、消息**。 |
| **标记已读** | `markMessageRead(caller, messageId, readTimestampMs)` | |
| **未读列表（角标/会话列表）** | `getUnreadSessionMessageIds(caller, imSessionId, limit)` | 返回 **messageId** 列表。 |
| **回复（引用）** | 先发消息 → **`storeMessageReplyRelation`**（Ed25519 + canonical）→ 展示侧 **`getMessageReplyRelation`** | 须 **MM1 验签** 链路，禁止绕过直调 MM2（**§0 第4条**）。 |
| **编辑** | **`editMessage`**（canonical + 公钥来自 **`storeUserData` type `0x45444A31`**）→ **`getMessageEditState`** | 规则：**editCount&lt;3**、时间窗等见 **§2.2**。 |
| **删除 / 撤回** | **`deleteMessage`** 或 **`recallMessage`**（同一套 **senderId + 签名** 约束） | 走 **`MessageRecallManager`**。 |
| **会话心跳 / 是否活跃 / 清理过期会话** | **`touchSession`**、`getSessionStatus`、`cleanupExpiredSessions` | IM 通道活跃表见 **`03-Business/04-Session.md`**、**§2.7**。 |
| **清理某会话过期消息** | **`cleanupSessionMessages(caller, imSessionId)`** | |
| **发图片/文件（分片）** | **`storeFileChunk`** → … → **`completeFile`**；异常 **`cancelFile`** | 续传断点：**`storeTransferResumeChunkIndex`** / **`getTransferResumeChunkIndex`** / **`cleanupTransferResumeChunkIndex`**；完整性见 **§2.6**、**`03-Storage.md`**。 |
| **好友** | **`sendFriendRequest`** → **`respondFriendRequest`**；列表 **`getFriends`**；删 **`deleteFriend`** | 均含时间戳 + Ed25519；见 **§2.4**。 |
| **群** | **`createGroup`**、**`inviteMember`**、**`removeMember`**、**`leaveGroup`**、**`getGroupMembers`**、**`updateGroupKey`** | 角色与 SQLite 校验见 **§2.5**。 |
| **群 @ / 禁言 / 群名** | **`validateMentionRequest`**（@ 前）→ **`recordMentionAtAllUsage`**（@ALL 后）；**`muteMember`** / **`isMuted`** / **`unmuteMember`**；**`updateGroupName`** / **`getGroupName`** | @ALL 速率窗持久化见 **`mm1_mention_atall_window`**。 |
| **多设备登录登记** | **`registerDeviceSession`**、**`updateLastActive`**、**`getDeviceSessions`**、**`cleanupExpiredDeviceSessions`** | Java 返回 **`null`/`byte[0]`/16B 踢出 id** 语义见 **§2.7**。 |
| **在线状态（最后已知）** | **`getUserStatus`** | 与服务端权威的关系见 **`UserStatusManager`** / **§2.7**。 |
| **用户扩展资料 / Ed25519 公钥** | **`storeUserData`** / **`getUserData`** / **`deleteUserData`** | **type** 由产品约定；编辑消息公钥 **type=`0x45444A31`**。 |
| **好友备注（加密笔记）** | **`updateFriendNote`** | 见 **§2.9**。 |
| **账号本地注销标记** | **`deleteAccount`**、**`isAccountDeleted`** | **非**全库擦除；见 **`06-AccountDelete.md`**。 |
| **主密钥 / 会话密钥** | **`generateMasterKey`**、**`refreshSessionKey`** | 见 **§2.9**；**禁止** JNI 侧绕过 **`MM1`** 失锁直访 **`GetKeyManagement()`**。 |
| **主密钥轮换** | **`rotateKeys`** | **`MM1::RefreshMasterKey()`**（锁内），**§2.9**。 |
| **TLS 证书固定（SPKI Pin）与封禁** | **`configurePinnedPublicKeyHashes`**、**`verifyPinnedServerCertificate`**、**`isClientBanned`**、**`recordFailure`**、**`clearBan`** | **`caller` 可空**等规则见 **`JniSecurityPolicy.h`**、**§2.9**。 |
| **JNI 环境自检** | **`validateJniCall()`** / **`validateJniCall(Class)`** | **§2.10**；退化路径，**不得**作为唯一安全边界。 |
| **运维 / 紧急** | **`cleanupExpiredData`**、**`optimizeStorage`**、**`getStorageStatus`**、**`getMessageCount`**、**`getFileCount`**、**`emergencyWipe`** 等 | 见 **§2.8**、**§2.9**。 |

### 0.5.1 本库**不**通过 JNI 提供的常见能力（须在 App / 服务端补齐）

以下在典型 IM 里很常见，但 **当前 `JniInterface` 无对应入口**（或仅部分相关），文档**不**应让读者误以为 native 已包圆：

| 能力 | 说明 |
|------|------|
| **用户注册 / 开户 / 忘记密码（B1）** | **§0.5.1.1**：业务域在 **HTTPS + 服务端**；客户端用 **`auth`** 与 **`userId` + 不透明 `token`** 接入 native 会话。 |
| **实时在线推送 / WebSocket / FCM** | **无** JNI；收到推送后仍用 **`storeMessage`** 等写本地或只更新 UI。 |
| **服务端历史漫游全量同步协议** | native 提供 **拉取/存储原语**（**`listMessages*`**、**`getSessionMessages`** 等），**同步策略与游标持久化**由上层实现。 |
| **批量写消息（事务一批）** | **`MM2::StoreMessages`** 等 **C++ API** 存在时**未必**暴露 JNI；Java 侧若需批量须 **新增 JNI** 或循环 **`storeMessage`**（语义不同，见 **`05`**）。 |
| **语音通话 / 视频会议（B5）** | **§0.5.1.2**：RTC 与 ZChatIM **JNI 解耦**；无通话类 native API。 |
| **全文搜索 / 复杂筛选** | 以 **`MessageQueryManager`** 已有能力为界；更强搜索属产品扩展。 |

#### 0.5.1.1 用户注册、开户与忘记密码（B1）

**边界**：**`JniInterface` 不提供** `register` / `signUp` / `resetPassword` 等 JNI。账户生命周期（手机号/邮箱验证、人机验证、密码策略、风控）均属 **App + 业务 HTTPS API**。

**与本库的衔接（推荐顺序）**：

1. **注册 / 首次开户**：服务端创建账号并返回（或后续登录返回）**`userId`（16B 二进制，与全库 ID 约定一致）** + **长期或会话级不透明凭证**。
2. **客户端**：**`initialize` / `initializeWithPassphrase`** 成功（本地可信区 + MM2）。
3. **调用 `auth(userId, token, clientIp?)`**：将服务端签发的 **`token`** 作为 **`AuthSessionManager`** 要求的**不透明凭证**（长度、非全零、不得与 **`userId` 等长且逐字节相同** 等见 **`docs/03-Business/02-Auth.md`**、本文 **§2.1**）。成功则得到 **`callerSessionId`**，后续所有业务 JNI 首参携带该句柄。
4. **忘记密码 / 重置**：仍走 **服务端重置流程**；重置成功后下发**新 `token`（或短期换票链）**，客户端再次 **`auth`** 即可。**无需**也**不应**在 native 层实现「改密 API」替代服务端校验。

**代码闭环（与实现一致，避免「已登录却无法调业务」）**：

- **`JniBridge::Auth`**：若 **`m_initialized` 为 false**（**`initialize` / `initializeWithPassphrase` 未成功**），直接返回**空** `sessionId`（**`src/jni/JniBridge.cpp`**）。故 **必须先初始化桥接，再 `auth`**。
- **`AuthSessionManager::Auth`**：若 **`userId.size() != USER_ID_SIZE`（16B）**，返回空；**`token`** 须满足 **§2.1** / **`02-Auth.md` 第7.1节**（≥**`AUTH_OPAQUE_CREDENTIAL_MIN_BYTES`**、非全零等）。
- **仓库内另有** **`LocalAccountCredentialManager`**（本地口令注册/改密/恢复等）**源文件**，当前**未**接入 **`MM1` 门面**与 **`JniInterface` / `JniNatives`**，**Java/Kotlin 不可达**；与「无注册 JNI」**不矛盾**。若未来产品要暴露，须**新增 JNI** 并同步 **`01-JNI.md`**。

**可选后续**：**`storeUserData`** 绑定 **Ed25519 公钥（type `0x45444A31`）** 等，供 **`editMessage`** / **`sendFriendRequest`** 等链路的验签；与「是否已注册」正交，由产品在 **`auth` 成功后**按需写入。

#### 0.5.1.2 语音通话与视频会议（B5）

**边界**：**实时音视频（VoIP、群视频、屏幕共享等）不在 ZChatIM JNI 契约内**。本仓库 **不提供** 采集、编码、SRTP/WebRTC、厂商 RTC SDK 的封装或 JNI 入口。

**集成方式**：

- **信令**：由 **独立信令服务**（WebSocket/HTTP）与 **App 业务层**实现；可与 IM 服务端同域或分域。
- **媒体面**：使用 **WebRTC、即构、声网、运营商 SDK** 等，与 **`JniBridge` / MM1 / MM2** 进程内可并存，但**无强制数据路径**经过 **`storeMessage`**。
- **聊天记录中的「通话记录」**（可选）：若产品要在会话里显示「语音聊天 3 分钟」等，可自行定义 **消息 payload** 走 **`storeMessage`**，或仅服务端漫游、**本地不落 native**——均属产品协议，**非**本库内置类型。

**小结**：B5 与 B1 类似，属于 **App + 第三方或服务端栈**；ZChatIM native 专注 **本地可信存储、会话、消息、好友/群、文件分片、证书 Pin** 等已列 API。

**避免与源码目录误解**：**`ZChatIM/src/mm1/managers/`** 下存在 **`VoiceVideoCallManager`、`RtcCallManager`、`RtcCallSessionManager`、`MediaCallCoordinator`** 等，头文件注明 **无 RTP/WebRTC 媒体面**、仅为**进程内呼叫状态 / callId 占位**；上述 API **未**出现在 **`JniInterface.h`** 与 **`jni/JniNatives.cpp`**，**不属于 JNI 契约**。grep 到这些文件与 **「B5：无通话类 JNI」** 同时成立。

### 0.5.2 产品 / UI 常见但**无专门 JNI**（由 App、服务端或约定 KV 补齐）

下列能力在成品 IM 里很常见；当前契约**没有**与之 1:1 的独立 native 方法，实现上通常走 **HTTP/实时通道**、**App 状态**或 **`storeUserData` 自定义 type**（需自行约定版本与迁移）。

| 场景 | 建议 |
|------|------|
| **草稿箱 / 发送队列 / 失败重试** | App 内存或自建存储；成功后再调 **`storeMessage`** / 文件分片 API。 |
| **正在输入（typing）** | 无 JNI；走服务端推送/WebSocket，UI 只展示。 |
| **会话列表：置顶、免打扰、归档、隐藏、排序** | 无专用 JNI；可用 **`storeUserData`** 按 **userId + 约定 type** 持久化，或仅存 App DB。 |
| **删除或「清空」整个会话** | 无单一「删会话」JNI；策略由产品定（逐条 **`deleteMessage`**、或仅清 UI + 服务端同步策略等）。 |
| **转发 / 合并转发 / 导出聊天记录** | 无专用转发 JNI；可多条 **`storeMessage`** 或上层打包 **payload**；导出多为 App 侧组装。 |
| **红包、转账、业务卡片、结构化消息** | native 不解析业务语义；密文/明文 **payload** 与协议由上层定义。 |
| **黑名单、联系人分组/标签** | 无专用 JNI；服务端权威 + 本地缓存或 **`storeUserData`** 约定 type。 |
| **群公告、邀请链接、二维码入群** | 多为**服务端**；若需本地草稿可用 KV 扩展。 |
| **群主转让、管理员升降级** | 以 **`GroupManager`** + **`group_members.role`** 的**实际 C++ 能力**为准；若无单独 JNI，可能落在既有 **`inviteMember`/`removeMember`** 等流程或尚未暴露——以源码与 **`01-MM1.md`** 为准。 |
| **多端已读回执「对方已读」展示** | 服务端同步已读事件；本地仍用 **`markMessageRead`** 更新己方状态。 |
| **换机迁移、备份与恢复** | 密钥 + 数据目录策略；无统一「一键迁移」JNI。 |
| **弱网、仅 WiFi 下载媒体、重试策略** | 上层网络层；**`getStorageStatus`** / 计数类 API 可辅助「空间不足」提示。 |
| **缩略图、图片压缩、音视频转码** | 上层（native 仅收/存 **chunk** 与 **sha256** 等）。 |
| **应用前后台切换** | 无专用 API；需要保活会话时可顺带 **`touchSession`**（**§2.7**）。 |

### 0.5.3 阅读顺序建议

1. 本表（**§0.5**）定路线 → 2. **`01-JNI.md`** 查签名、**principal**、返回编码 → 3. 本文 **§2** 查安全细则与禁止项 → 4. **§0.5.1～0.5.2** 区分「架构不提供」与「无专门 JNI」以免误期。

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
安全注意：未 `Initialize` 成功前，其余 JNI 入口应拒绝或返回失败语义。**桥接层**用 **`std::atomic<bool>`** 表示「已 **`Initialize` 成功」**，避免与其它线程无锁状态查询发生数据竞争（见 **§0 第7条**）。
Java：`com.yhj.zchat.jni.ZChatIMNative.initialize(String, String)`；native 由 **`jni/JniNatives.cpp`** **`RegisterNatives`** 绑定。

#### `initializeWithPassphrase(dataDir, indexDir, messageKeyPassphrase) -> true/false` / `InitializeWithPassphrase(..., const char*)`（C++）
职责：同 **`initialize`**，但将 **UTF-8 口令 C 串**（可为 **`nullptr`**）传入 **`MM2::Initialize`**，用于 **ZMKP** 新库或解锁已有 **ZMKP** 的 **`mm2_message_key.bin`**（须 **`ZCHATIM_USE_SQLCIPHER=ON`**；空串口令由 MM2 拒绝）。**`passphrase == null`** 时 native 传 **`nullptr`**，与 **`initialize`** 等价。
Java：`ZChatIMNative.initializeWithPassphrase(String, String, String)`。

#### `cleanup() -> void` / `Cleanup()`（C++）
职责：与 `initialize` 对称释放资源。

### 2.1 认证模块
#### `auth(userId, token, clientIp?) -> sessionId/null`
职责：对用户进行身份认证并建立会话。
安全注意：限流/封禁由 `mm1::AuthSessionManager` 按 `docs/03-Business/02-Auth.md` 实现；JniBridge 不应重复实现。**`token`**：不透明凭证须 **`≥ Types::AUTH_OPAQUE_CREDENTIAL_MIN_BYTES`（32）**、**非全零**，且**不得**与 **`userId` 等长且逐字节相同**（**`VerifyCredential`**）。`clientIp` 为空时仅**用户级** 10 次/分钟；**非空**（建议 IPv4 4 字节 / IPv6 16 字节）时额外启用 **IP 级** 5 次/分钟，且封禁索引为 `userId‖clientIp`。
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
安全注意：**当前 `JniBridge`**：`callerSessionId` 与 `sessionIdToDestroy` **均须有效**，且 **`TryGetSessionUserId` 解析出的 `principal` 须相同**（与 **`01-JNI.md`「双会话同一 principal」** 一致）；**无**单独的运维/RBAC 代销毁路径。
入参：`callerSessionId`、`sessionIdToDestroy`。
返回：true 成功，false 失败。
路由：**已实现** — **`JniBridge` → `AuthSessionManager::DestroySession(sessionIdToDestroy)`**（前置双会话 principal 校验）。

### 2.2 消息模块
#### `storeMessage(callerSessionId, imSessionId, payload) -> msgId/null`
职责：存储一条消息密文到 MM2（**进程内 RAM**，不落 SQLite **`im_messages`**）。
安全注意：须先 **`TryBindCaller`**（有效 caller）；**`imSessionId`** 为聊天会话通道（**16B**，非 ZSP 头 4 字节）；**当前 `JniBridge` 不**校验 **`imSessionId == principal`**（见 **`01-JNI.md` 绑定矩阵**）。**`principal`（16B）作为本条消息的 `senderUserId`** 写入 **MM2 RAM IM 行**（与历史 **`im_messages.sender_user_id`** 语义一致），供 **`MessageEditManager` / `MessageRecallManager`** 与 API 中的 **`senderId`** 做 **`ConstantTimeCompare`**。客户端**发送**消息时须使用**与内容作者一致**的会话/`Auth` **`userId`**。
入参：`callerSessionId`、`imSessionId`、`payload`。
返回：`msgId` 成功返回，空 vector 表示失败。
路由：**已实现** — **`JniBridge` → `MM2::StoreMessage(imSessionId, principal, payload, out)`**。

#### `retrieveMessage(callerSessionId, messageId) -> data/null`
职责：根据消息 ID 检索消息内容（密文或封装数据）。
安全注意：须先 **`TryBindCaller`**；**当前 `JniBridge` 不**按 **`messageId`/会话**做 **principal 可见性**校验（任何有效 caller 若能猜到 id 即可触发 MM2 读取；产品层须自限或未来在桥接层收紧）。
入参：`callerSessionId`、`messageId`。
返回：data 或空向量。
路由实现规划：调用 `mm2::MM2::RetrieveMessage(...)`。

#### `deleteMessage(callerSessionId, messageId, senderId, signatureEd25519) -> true/false`
职责：安全删除/撤回等价能力入口（契约语义对齐 MessageRecall）。
安全注意：**`senderId` MUST 与 principal 一致**；**`MessageRecallManager`** 先比对 **RAM 内 `senderUserId`**（**`MM2::GetMessageSenderUserId`**）与 **`senderId`**，再 **`ZChatIM|RecallMessage|v1`** **Ed25519** 验签；最后 Level2 覆写。须走 `mm1::MessageRecallManager`，禁止直调 **`MM2::DeleteMessage`**。
入参：`callerSessionId`、`messageId`、`senderId`、`signatureEd25519`。
返回：true 成功，false 失败/校验未通过。
路由实现规划：调用 `m_mm1.GetMessageRecallManager().DeleteMessage(...)`（或等价 Recall 语义）。
禁止项：禁止直接调用 `mm2::MM2::DeleteMessage(msgId)`。

#### `recallMessage(callerSessionId, messageId, senderId, signatureEd25519) -> true/false`
职责：安全撤回消息（Level2 覆写与索引状态更新）。
安全注意：同 **`deleteMessage`**（**`sender_user_id`** 绑定 + 验签 + **`MessageRecallManager`**）。
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
职责：消息同步按时间戳游标拉取（**RAM 行 `stored_at_ms`**：本地 **`StoreMessage`** 写入时刻的 Unix 毫秒；**`>= sinceTimestampMs`** inclusive；与历史 SQLite 列语义一致）。
入参：`callerSessionId`、`userId`、`sinceTimestampMs`（毫秒 epoch）、`count`。
返回：二维数组。
路由实现规划：走 `mm2::MM2::GetMessageQueryManager().ListMessagesSinceTimestamp(...)`（已实装）。

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
安全注意：**`senderId` MUST 与 principal 一致**；**`signature`（64B Ed25519 分离签名）** 在 **canonical payload** 上验证；**公钥**来自 **`UserDataManager::GetUserData(senderId, 0x45444A31)`**（32B）。
**Canonical payload（定长拼接，无额外分隔符）**：ASCII **`ZChatIM|EditMessage|v1`** ‖ **`messageId`(16)** ‖ **`senderId`(16)** ‖ **`editTimestampSeconds`（u64 大端）** ‖ **`SHA-256(newEncryptedContent)`（32）**。
业务规则（与 **`MessageEditManager::ApplyEdit`** 一致）：**`edit_count < 3`**；若 **`edit_count > 0`** 则 **`editTimestampSeconds ≥ lastEdit`** 且 **`editTimestampSeconds - lastEdit ≤ 300`（秒）**；成功则 **`MM2::EditMessage(..., newEditCount = c+1)`**。**`CheckEditAllowed`** 仅状态/时间窗（**不**验签）。
入参：`callerSessionId`、`messageId`、`newEncryptedContent`、`editTimestampSeconds`、`signature`、`senderId`。
返回：true 成功，false 失败。
路由：**已实现** — `JniBridge` → **`m_mm1.GetMessageEditOrchestration().EditMessage(...)`** → **`MessageEditManager::ApplyEdit`**。

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
**已实现**：`JniBridge::StoreUserData` → `mm1::UserDataManager::StoreUserData` → **`MM2::StoreMm1UserDataBlob`**（**MM2 已 Initialize** 时写入元库表 **`mm1_user_kv`**，见 **`docs/02-Core/03-Storage.md` 第2.6节**；元库 **`PRAGMA user_version=11`**）；**MM2 未初始化**时回退进程内内存表。

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
职责：校验 @ 触发权限与成员合法性；**成员/角色**经 **`MM2` → `group_members` SQL**。**@ALL** 时校验 **owner/admin** 与 **60s 滑动窗内已用次数 <3**（与 **`recordMentionAtAllUsage`** 共享元库 **`mm1_mention_atall_window`**，**`user_version=11`**；**进程重启可恢复**，须 **`MM2::Initialize`**）。
安全注意：**`senderId` MUST 与 principal 一致**；**`signatureEd25519`（64B）** 验签；**公钥 `0x45444A31`**。
**Canonical payload**：ASCII **`ZChatIM|MentionRequest|v1`** ‖ **`groupId`(16)** ‖ **`senderId`(16)** ‖ **`mentionType`（i32 BE）** ‖ **`nowMs`（u64 BE）** ‖ **`count`（i32 BE，`mentionedUserIds.size()`）** ‖ 每个 **`mentionedUserId`(16)**。**`mentionType=1`**：被 @ 用户须均在群且列表非空。**`mentionType=2`**：**@ALL**，发送者须 **admin/owner**。
路由：**已实现** — **`mm1::MentionPermissionManager::ValidateMentionRequest`**。

`recordMentionAtAllUsage(callerSessionId, groupId, senderId, nowMs) -> true/false`
职责：向 **(groupId, senderId)** 的 **@ALL** 速率窗追加一次 **`nowMs`**（窗内 **≥3** 则失败）；须发送者在群。
安全注意：**建议**在 **`validateMentionRequest`（type=2）** 通过后立即调用，且 **`nowMs`** 与签名载荷一致，否则可能出现「校验通过但记录失败」或窗状态与签名不一致。
路由：**已实现** — **`MentionPermissionManager::RecordMentionAtAllUsage`**。

#### GroupMute
`muteMember(callerSessionId, groupId, userId, mutedBy, startTimeMs, durationSeconds, reason) -> true/false`
`isMuted(callerSessionId, groupId, userId, nowMs) -> true/false`
`unmuteMember(callerSessionId, groupId, userId, unmutedBy) -> true/false`
职责：禁言、查询禁言状态、解禁。
安全注意：**`mutedBy` / `unmutedBy`** 须为群内 **owner/admin**；**admin** 仅可禁言 **member**；**不可**禁 **owner**。**`IsMuted`** 仅校验 **caller**。
路由：**`JniBridge` → `mm1::GroupMuteManager`** → **`MM2` / `mm2_group_mute`**（**`03-Storage.md`**）。**`removeMember` / `leaveGroup`** 经 **`MM2::DeleteGroupMemberForMm1`** 会先 **`DELETE` 对应 `(group_id, user_id)` 禁言行**，再删 **`group_members`**，避免退群/踢人后残留禁言索引。

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

**持久化**：上述多设备 API 经 **`MM2` → `SqliteMetadataDb`** 写 **`mm1_device_sessions`** 等（元库 **`user_version=11`**，见 **`03-Storage.md`**）；须 **`initialize`（MM2）已成功**。**`EmergencyWipe` / `CleanupAllData`** 删除元库文件后登记清空。**`TouchSession` / `GetSessionStatus`（IM 通道）** 对应 **`mm1_im_session_activity`**，规则同 **`docs/03-Business/04-Session.md` 第七节**。
`getUserStatus(callerSessionId, userId)`：在线/离线，走 MM1 **`UserStatusManager`** → **`mm1_user_status`**（**最后已知**缓存；**服务端**权威，须 **`MM2::Initialize`**）。
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
`generateMasterKey(callerSessionId)`：走 **`MM1::GenerateMasterKey()`**（生成后**写入** `KeyManagement` 主密钥槽，**`getStatus` 的 `mm1_master_key_present`** 为 **`1`**；**禁止** JNI 侧绕过 `MM1` 失锁调 `GetKeyManagement()`）。

`refreshSessionKey(callerSessionId)`：走 **`MM1::RefreshSessionKey()`**（递归锁内委托 `KeyManagement::RefreshSessionKey`）。
`emergencyWipe(callerSessionId)`：**`JniBridge`** → **`MM1::EmergencyTrustedZoneWipe`**（清库 + Auth + 多设备 + 在线 + 证书 Pin + IM 活跃 + @ALL 限速 + 主密钥 + **`MM1::Cleanup`**）+ 桥接去初始化（**`m_initialized.store(false, std::memory_order_release)`**，**`std::atomic<bool>`**）。**纯 C++** 若仅 **`SystemControl::EmergencyWipe`** 则**不**改 **`JniBridge::m_initialized`**（见 **`01-MM1.md`**）。
`getStatus(callerSessionId)`：返回 **`jni_bridge_initialized` / `mm2_initialized` / `mm1_master_key_present`**（字符串 **`0`/`1`**）。
`rotateKeys(callerSessionId)`：走 **`MM1::RefreshMasterKey()`**（锁内）。

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
职责：本地注销标记与查询（**非**全库自动擦除）。
安全注意：**`userId` MUST 与 principal 一致**；**`reauthToken` 与 `secondConfirmToken` 须同长 ≥16B 且逐字节相同**（**`ConstantTimeCompare`**）。成功则写 **`mm1_user_kv`** **`type=0x41434431`（'ACD1'）** 墓碑 **`{1}`**；**不**调用 **`MM2::CleanupAllData`**。
路由：**已实现** — **`mm1::AccountDeleteManager`**。

#### 好友备注
`updateFriendNote(callerSessionId, userId, friendId, newEncryptedNote, updateTimestampSeconds, signatureEd25519)`
职责：更新好友备注（密文/明文由上层定义，本层对 **`newEncryptedNote`** 做 **SHA-256** 纳入签名）。
安全注意：**`userId` MUST 与 principal 一致**；**公钥 `0x45444A31`**；**`friendId`** 须在 **`FriendManager::GetFriends(userId)`**（accepted）中。
**Canonical payload**：**`ZChatIM|UpdateFriendNote|v1`** ‖ **`userId`(16)** ‖ **`friendId`(16)** ‖ **`updateTimestampSeconds`（u64 BE）** ‖ **`SHA-256(newEncryptedNote)`（32）**。
持久化：**`mm1_user_kv`** **`type=0x464E424E`（'FNBN'）**，**`ZFN1`** + **`friendId(16)‖len u32 BE‖note`**，单条 **note ≤64KiB**。
路由：**已实现** — **`mm1::FriendNoteManager`**。

### 2.10 JNI 调用验证
`validateJniCall() -> true/false`
职责：验证 JNI 调用合法性（例如检查初始化状态、JNI 环境/类是否正确等）。
安全注意：由于本项目暂时移除 JNI 依赖，validate 的强度取决于未来 JNI 适配层传入 env/class 的实现方式。实现方不得仅返回固定值。

`validateJniCall(jniEnv, jclass) -> true/false`（`JniInterface`/`JniBridge` 以 `void*` 承载指针，避免头文件依赖 `jni.h`）
职责：**优先在 JNI 入口使用**：将 env/class 转交 `mm1::MM1::ValidateJniCall(env, class)`（或等价 `JniSecurity` 校验）。
无参版本仅可作为退化路径（如非 JNI 调用方），不得作为唯一安全边界。

**`JniSecurity` 辅助堆缓冲区**（非 JVM 直接缓冲）：**`AllocateJniMemory` / `FreeJniMemory`** 当前委托 **`common::Memory::Allocate` / `Free`**（分配成功则 **`SecureZero`**）；契约见 **`include/mm1/JniSecurity.h`**、**`docs/07-Engineering/02-Cpp-Completion-Roadmap.md` M0**。

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


