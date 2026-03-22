# ZChatIM C++ 实现状态（活文档）

**作用**：对照 **`ZChatIM/include/`** 与 **`ZChatIM/src/`**，记录「已实现 / 桩 / 未接」，避免头文件扩张与文档漂移。  
**维护**：完成功能或删改 API 时顺手改本页对应行；详细行为仍以 **`03-Storage.md`**、**`04-ZdbBinaryLayout.md`**、**`MM2.h`** 与源码为准。**章节引用**与 **`docs/README.md` 第5节**一致：统一 **「第×节 / 第×条」**，不用西文章节号。  
**持久化**：**凡新增/扩展 SQLite、`.zdb` 写入**须对照 **`docs/README.md` 第2节** 与根 **`README`**——**持久化从严**；**能内存则内存**。  
**文档冲突**：凡 **`04-Features` / 总览** 与「消息是否落盘、重启是否丢」矛盾时，**先读** **`docs/README.md`**「**冲突与权威**」，**再以本页 + 第8节 为准**。  
**「彻底搞定 C++」迭代范围**：见 **`docs/07-Engineering/02-Cpp-Completion-Roadmap.md`**（**M0–M3**；与 **`01-MM1.md` 愿景**区分）。

**本版本（当前迭代）交付口径 — 就按这个结案**：以 **`ZChatIM --test` 全绿** + 本页 **第7节（P0/P1/P2）勾选** + **`01-MM1.md` 一点五.3（M3 非本期 C++ 义务）** 为 **C++ 侧发版边界**。**不**在本版本实现 **M3 协议栈**（双棘轮、Random Allocator 愿景模型等）；若产品改口径须新开版本并改 **`05` / 一点五.3**。  
**安全子模块（`SideChannel` / `SecurityMemory`）**：**`MM1_security_submodules.cpp`** 已强化——**`SecurityMemory::Lock/Unlock`** 与 **`IsLocked`**（区间须落在登记范围内）、**`Free`/`Reallocate` 释放跟踪锁**、**`ReleaseAllLockTracking`**（**`EmergencyTrustedZoneWipe`** 调用）；**`SideChannel`**：**有界 `AntiTimingDelay`**、**CSPRNG `RandomDelay`**、**`FlushCache`**（**x86** 尽力 **`CLFLUSH`** + 栅栏 + _scratch 读穿透）、**`PreventCacheSideChannel`**。**非魔法**：不保证抵御全部微架构侧信道；**热路径比对**仍以 **`common::Memory::ConstantTimeCompare`** / **OpenSSL** 为准（**`01-MM1.md` 一点五.2 第七节**）。

### 如何阅读（约 1 分钟）

1. **存储与 `.zdb`**：看 **第2.1节 容器 / 元数据** + **`03-Storage.md` 第七节** + **`04-ZdbBinaryLayout.md`**。  
2. **哪些 API 已能调用**：看 **第2.1节 编排**；**MM2 行为细则与边界**：看 **第2.2节**；**MM2 是否依赖 `common` 工具**：看 **第2.4节**；**JNI / MM1 进度**：看 **第4节**、**第3节**、**第7节**。  
3. **失败与并发**：看 **第8节**（部分失败路径、锁、`MessageQueryManager`）。  
4. **JNI / Java**：**第4节**（**`RegisterNatives` + `ZChatIMNative.java`** 已接 **`JniInterface`/`JniBridge`**）；契约以 **`docs/06-Appendix/01-JNI.md`** + **`JniInterface.h`** 为准。  
5. **下一步做啥**：**第7节 勾选清单**。

---

## 1. 构建产物

（与 **`ZChatIM/CMakeLists.txt`** **`add_library` / `add_executable`** 显式列表 **一字对齐**；不用目录通配符概括。）

| 目标 | 说明 |
|------|------|
| **`sqlite3`**（静态库） | **仅当 `ZCHATIM_USE_SQLCIPHER=OFF`**：**`thirdparty/sqlite/sqlite3.c`**（**C99**），**`target_link_libraries(ZChatIMCore PRIVATE sqlite3)`**。 |
| **SQLCipher** | **默认 `ZCHATIM_USE_SQLCIPHER=ON`**：树内 **`sqlite3.c` + `sqlite3.h`**（**默认目录** **`thirdparty/sqlcipher/`**；**Windows** 若存在 **`prebuilt/windows-x64/amalgamation/`** 则**优先**）→ **`add_library(zchatim_sqlcipher STATIC …)`**（**无 vcpkg**）。**不**与明文 **`thirdparty/sqlite/sqlite3.c`** 同时用于元数据。 |
| **`ZChatIMCore`**（静态库） | **37**（**`APPLE` 目标 +1：`src/mm2/MM2_message_key_darwin.cpp` → 共 38**；以 **`CMakeLists.txt`** **`target_sources(ZChatIMCore …)`** 为准）个 **`.cpp`**（**全 C++**）：**`src/common/`** — `Utils.cpp`、`Ed25519.cpp`（**OpenSSL 3 EVP**）、`Memory.cpp`、`String.cpp`、`File.cpp`、`Time.cpp`、`Random.cpp`、`Logger.cpp`；**`src/mm1/`** — `MM1.cpp`、`MM1_security_submodules.cpp`、`managers/AuthSessionManager.cpp`、`managers/MessageReplyManager.cpp`、`managers/MessageRecallManager.cpp`、`managers/UserDataManager.cpp`、**`managers/FriendManager.cpp`**、**`managers/FriendVerificationManager.cpp`**、**`managers/GroupManager.cpp`**、**`managers/GroupNameManager.cpp`**、**`managers/GroupMuteManager.cpp`**、**`managers/MessageEditManager.cpp`**（含 **`MessageEditOrchestration::EditMessage`**）、**`managers/MentionPermissionManager.cpp`**、**`managers/FriendNoteManager.cpp`**、**`managers/AccountDeleteManager.cpp`**、`managers/MM1_manager_stubs.cpp`、`managers/SessionActivityManager.cpp`；**`src/jni/`** — `JniBridge.cpp`、`JniInterface.cpp`；**`src/mm2/`** — `MM2.cpp`、**`MM2_message_key_passphrase.cpp`（ZMKP v1）**；**`src/mm2/crypto/`** — `Sha256.cpp`；**`src/mm2/storage/`** — `Crypto.cpp`、`BlockIndex.cpp`、`MessageQueryManager.cpp`、`SqliteMetadataDb.cpp`、`StorageIntegrityManager.cpp`、`ZdbFile.cpp`、`ZdbManager.cpp`。**`target_compile_definitions`**：**`ZCHATIM_USE_SQLCIPHER=1`**、**`SQLITE_HAS_CODEC=1`**（**`sqlite3_key_v2`** 等 API 在 **`SqliteMetadataDb.cpp`** 含 **`sqlite3.h`** 时可见）。**`target_link_libraries`**：**`PUBLIC`** **`OpenSSL::Crypto`**；**元数据 SQLite** **`PRIVATE`** **`sqlite3` / `zchatim_sqlcipher`**；**Windows** **`PUBLIC`** **`crypt32`**（**DPAPI**）。 |
| **`ZChatIM.exe`** | **`ZCHATIM_BUILD_EXE=ON`**（默认）时生成。**源**：**`main.cpp`**；**`ZCHATIM_BUILD_TESTS=ON`**（默认）时追加 **`tests/*.cpp`**。**`PRIVATE` 链接** **`ZChatIMCore`**。**OFF** 时不创建该可执行目标。 |
| **`ZChatIMJNI`**（可选） | **`ZCHATIM_BUILD_JNI=ON`**（默认）且 **`find_package(JNI)`** 或 **`JAVA_HOME`** 兜底成功时生成。**源**：**`jni/ZChatIMJNI.cpp`**（**`JNI_OnLoad`** 调 **`zchatim_RegisterNatives`**）、**`jni/JniNatives.cpp`**（**`RegisterNatives`** → **`JniInterface`**，类 **`com.yhj.zchat.jni.ZChatIMNative`**）。**`PRIVATE` 链接** **`ZChatIMCore`**。Java 侧：**`ZChatServer/.../com/yhj/zchat/jni/ZChatIMNative.java`**。 |

**树内自检**：**`ZCHATIM_BUILD_TESTS=ON`**（默认）时支持 **`--test*`**；Ed25519 块需 **`__has_include(<openssl/evp.h>)`**（与生产均为 **OpenSSL**）。见 **`docs/07-Engineering/01-Build-ZChatIM.md` 第7节**。  
**树内测试入口**：**`ZChatIM.exe --test`** 跑全量（含 **`RunCommonToolsTests`**、MM1/MM2 各 **`[CASE]`**、**原 minimal 场景**（**`RunMinimalScenarioTestCasesMerged`**：文件分片 / CompleteFile / 消息批处理与回滚 / 已读等）、**`tests/mm2_fifty_scenarios_test.cpp`**、**`tests/jni_im_smoke_test.cpp`**）。其中 **JNI IM 冒烟**不经 JVM，直接 **`JniInterface`**：**消息** + **UserData** +（**OpenSSL 可见时）**好友 **Send/Respond/GetFriends/Delete**（**`FriendManager` / `FriendVerificationManager`**）。

---

## 2. MM2（消息与存储）

### 2.1 已有 `.cpp` 且与文档对齐度较高

| 组件 | 头文件 | 源文件 | 状态 |
|------|--------|--------|------|
| 编排 | `mm2/MM2.h` | `MM2.cpp` | **核心路径**：`Initialize`（**`SqliteMetadataDb::InitializeSchema`（**v11**）→ `StorageIntegrityManager::Bind` → `ImRamClearUnlocked()` → `m_initialized=true` → `MessageQueryManager::SetOwner(this)`**）、`StoreFileChunk`/`GetFileChunk`、**`CompleteFile`/`CancelFile`/续传索引**、`Cleanup`/`CleanupAllData`、`GetStorageStatus`；**IM**：**`StoreMessage`/`StoreMessages`/`RetrieveMessage`/`RetrieveMessages`/`GetSessionMessages`/`DeleteMessage`** 与 **`GetMessageQueryManager()::ListMessages*`**（**AES-GCM**；**OpenSSL 3**；**仅 RAM / ImRam**）；**`MarkMessageRead`/`GetUnreadSessionMessages`**；**`StoreMessageReplyRelation`/`GetMessageReplyRelation`**、**`EditMessage`/`GetMessageEditState`**、**`CleanupSessionMessages`**；**`StoreFriendRequest`/`UpdateFriendRequestStatus`/`DeleteFriendRequest`/`CleanupExpiredFriendRequests`**；**`CreateGroupSeedForMm1`/`UpsertGroupMemberForMm1`/`DeleteGroupMemberForMm1`/`ListGroupMemberUserIdsForMm1`/`GetGroupMemberRoleForMm1`/`GetGroupMemberExistsForMm1`/`UpsertGroupKeyEnvelopeForMm1`/`TryGetGroupKeyEnvelopeForMm1`**（**MM1 `GroupManager`**）；**`SeedAcceptedFriendshipForSelfTest`**（**自测注入**；符号在**正式库亦存在**，**禁止 JNI** 接线，见 **`JniSecurityPolicy.h`**）+ **`UpdateGroupName`/`GetGroupName`**（**`mm2_group_display`**；**`UpdateGroupName`** 经 **MM1 `GroupNameManager`**）；**`CleanupExpiredData`**（当前主要清过期 **pending** 好友请求）、**`OptimizeStorage`**（**`VACUUM`**）；**`GetMessageCount`**→**进程内 IM RAM 条数**（**非** SQLite **`im_messages`** 热路径）；**`ZCHATIM_USE_SQLCIPHER`**（默认 ON）时 **`Initialize`** 内 **`Crypto::Init`** + **`mm2_message_key.bin`** + **`SqliteMetadataDb::Open(…, key)`**；**否则** **`Crypto`/密钥** 仍 **懒加载**至 **`EnsureMessageCryptoReadyUnlocked`**（见 **`03-Storage.md` 第七节**）。 |
| 元数据 | `mm2/storage/SqliteMetadataDb.h` | `SqliteMetadataDb.cpp` | **完成**：**默认 SQLCipher**（**`Open(path,key32)`**、**`03-Storage.md` 第4.2节 PRAGMA**、**明文 SQLite → `sqlcipher_export` 迁移**）；**`InitializeSchema`**：**当前 schema + `PRAGMA user_version=11`**（含 **`mm1_device_sessions` / `mm1_im_session_activity` / `mm1_cert_pin_*` / `mm1_user_status` / `mm1_mention_atall_window`**），**不建** **`im_messages` / `im_message_reply`**（**无** IM 相关迁移/单测建表分支）。**`ZCHATIM_USE_SQLCIPHER=OFF`** 时为 **vanilla `sqlite3.c`** + **`Open(path)`**。 |
| 完整性 | `mm2/storage/StorageIntegrityManager.h` | `StorageIntegrityManager.cpp` | **完成**：SHA-256 + 与 SQLite 联动。 |
| 容器 | **`mm2/storage/ZdbFile.h`**、**`mm2/storage/ZdbManager.h`** | **`ZdbFile.cpp`**、**`ZdbManager.cpp`** | **完成**：v1 布局、**`Create` 随机预填 payload**、`AppendRaw`/读写/删除等（见 **`04-ZdbBinaryLayout.md`**）。 |
| 加密 | `mm2/storage/Crypto.h` | `Crypto.cpp` | **全平台 OpenSSL 3**（AES-GCM、PBKDF2、**`RAND_bytes`**；**Unix** 可再读 **`/dev/urandom`**）。**`Encrypt* / Decrypt* / DeriveKey`** 须 **`Init`**；**`GenerateSecureRandom` / `HashSha256`** 不依赖 **`s_initialized`**。**`GenerateSecureRandom`**：两轮 **`RAND_bytes`**（间 **`RAND_poll`**）+ Unix **`ReadDevUrandom`** 仍失败则返回**空向量**（**`ZdbFile::Create`** 等据此失败）。 |
| 哈希 | `mm2/crypto/Sha256.h` | `Sha256.cpp` | **OpenSSL 3** **`EVP_sha256`**（**`Sha256` / `Sha256Hasher`**）。 |
| 其它存储辅助 | **`mm2/storage/BlockIndex.h`**、**`mm2/storage/MessageQueryManager.h`** | **`BlockIndex.cpp`**、**`MessageQueryManager.cpp`** | **`BlockIndex`**：仍为**桩**；**文件块索引由 `SqliteMetadataDb`/`data_blocks` 承担**，**`MM2` 不嵌入 `BlockIndex` 成员**。**`MessageQueryManager`**：**`SetOwner(this)`** 在 **`MM2::Initialize`** 成功路径末尾（**`m_initialized = true` 之后**）；**`SetOwner(nullptr)`** 在 **`MM2::CleanupUnlocked`**。**`List*`** 均经 **`MM2` 持锁** 走 **RAM IM 索引**（**`ImRamListIds*Unlocked`** + **`RetrieveMessage`**），**不**读 SQLite **`im_messages`**。返回行编码见 **`MessageQueryManager.h`**。 |

### 2.2 `MM2` 扩展语义备忘（非「未实现」清单）

- **`CompleteFile`**：按 **连续** chunk 索引 **0..N-1** 读取并计算 **SHA-256**（流式 **`crypto::Sha256Hasher`**，**OpenSSL EVP**），与传入摘要一致后写入 **`mm2_file_transfer`**（**`ON CONFLICT`** 可补行）。若 chunk 序列有**空洞**，校验以**首个缺失**为界，可能不符合预期，产品层应保证顺序写满。
- **`CleanupExpiredFriendRequests` / `CleanupExpiredData`**：当前策略为删除 **`status=0`（pending）** 且 **`created_s` 早于 `now - 30 天`** 的行（秒级时间轴）；可按产品再调 **`ttl`** 或扩展其它表。
- **`EditMessage`**：**不**重算 **`StoreMessage` 的 AES-GCM**；**`newEncryptedContent`** 为**已加密包**（与历史 **`.zdb` chunk0** 包**同字节上限**），**`edit_count`** 须在 **[1,3]**（上层规则仍以 MM1/JNI 为准）；**落点**为 **RAM `ImRamMessageRow.blob`**。
- **`GetTransferResumeChunkIndex`**：无 **`mm2_file_transfer`** 行时返回 **`false`**（与 JNI 侧 **`UINT32_MAX` 哨兵** 接线时须转换）。

### 2.3 已知限制（与 **`03-Storage.md` 第七节** 一致）

- v1 **无跨 `.zdb` 与 SQLite 的单事务**；**`PutDataBlockBlob` / `StoreFileChunk`** 在 **`WriteData` 成功后** 若元数据链失败会尽力 **`RevertFailedPutDataBlockUnlocked`**。**`StoreMessage`** 仅写 **RAM**，**不**参与上述磁盘事务。**覆盖写**在 **`Record` 前已 `DeleteData` 清零旧块**等路径仍可能产生**不可自恢复**的不一致，见 **第8节**。  
- 更细的**部分失败形态**见 **第8节（风险与部分失败路径）**。

### 2.4 MM2 与 `include/common` 工具：**依赖与边界**（与源码 `#include` 一致）

| 关系 | 说明 |
|------|------|
| **编译包含** | **`mm2/MM2.h`** 仅从 **`common/`** 包含 **`JniSecurityPolicy.h`**（策略/锁约定注释与常量）。**`src/mm2/*.cpp`** **未** `#include` **`common/{Utils,Memory,String,File,Time,Random}.h`**，**未**使用 **`Logger.h`** / **`LOG_*`**。 |
| **链接** | **`ZChatIMCore`** 同时编入 **MM2** 与 **`src/common/*.cpp`**。**`src/mm2/*.cpp`** 与 **`src/common/*.cpp`** 之间**无**直接 `#include`/调用；**`src/mm1/*`**（如 **`MessageReplyManager.cpp`**）**可** `#include` **`mm2/MM2.h`** 并在校验后调用 **`MM2::Instance()`**（见 **第3节**）。**可**被**同一进程**内其它代码（测试、未来 JNI）**分别**调用。 |
| **MM1 Ed25519** | **`common::Ed25519VerifyDetached`**（**`src/common/Ed25519.cpp`**）：**`OpenSSL::Crypto`**（**EVP_PKEY_ED25519**）。 |
| **随机数** | **`.zdb` 预填**（**`ZdbFile::Create`**）与 **MM2 消息/密钥路径**使用 **`ZChatIM::mm2::Crypto::GenerateSecureRandom`**（及 **`Init` 后**的加解密 RNG），**不**经过 **`ZChatIM::common::Random`**。两套 RNG **实现分离**（**`mm2::Crypto`** vs **`common::Random`**，**状态与 API 不共享**）。**`mm1::SecureRandom::GenerateInt64` / `GenerateUInt64`**：**`mt19937_64`**，**仅启动时**用 **`Crypto::GenerateSecureRandom(8)`** 播种（**非**每调用 **RAND**）；**密钥材料**仍走 **`KeyManagement` → `mm2::Crypto::GenerateSecureRandom`**。 |
| **文件系统** | MM2 使用 **`std::filesystem`**、**`std::fstream`**（如 **`MM2.cpp`**、**`ZdbFile`**、**`SqliteMetadataDb::Open`** 路径），**不**使用 **`common::File`**。 |
| **内存清零** | MM2 内有 **`SecureZeroBytes`**（**`MM2.cpp`** 等）及 **`std::memset`/平台 API**；**不**使用 **`common::Memory`**。 |
| **接 JNI / 产品时** | 若要在 **MM2 热路径**打日志或复用 **`common::File`**，属**新依赖**，须自行评估锁顺序与 **`m_stateMutex`**；当前**无**此类接线。 |

---

## 3. MM1

**MM1 ↔ MM2**：MM1 为 **JNI 侧信任边界**（校验/会话/策略 + 安全子模块）；MM2 为 **持久化与 `Crypto` 生命周期所有者**。允许的 **mm1→mm2** 直连：**`mm2::Crypto`** + 文档明确的 **Manager→`MM2::…`**（禁止在 **JniBridge** 为省事先调 MM2 绕过 MM1）。**初始化**：**`MM1::Initialize`** 内 **`Crypto::Init`（幂等）**；**`MM2::Initialize`** 须在首次用 MM2 存储前成功；**`MM1::Cleanup`** **不** **`Crypto::Cleanup`**。**锁顺序**：需同时动 MM1/MM2 时 **先 MM1 `m_apiRecursiveMutex`（或等价的 JniBridge 锁）再进 MM2**，勿在持有 **`MM2::m_stateMutex`** 时重入 MM1（见 **`include/common/JniSecurityPolicy.h` 第6条与第7条**）。

| 组件 | 状态 |
|------|------|
| **`AuthSessionManager`** | **已实现**（`src/mm1/managers/AuthSessionManager.cpp`）：**`Auth`/`VerifySession`/`DestroySession`** + **`TryGetSessionUserId(sessionId, out)`** + **`ClearAllSessions`**（会话清零 + 限流/封禁表清空）；**`VerifyCredential`** 按 **`Types::AUTH_OPAQUE_CREDENTIAL_MIN_BYTES`（32）** 等规则（见 **`02-Auth.md` 第7.1节**）。**`EmergencyTrustedZoneWipe` / `JniBridge::EmergencyWipe`** 内经由 **`MM1::ClearAllAuthSessions()`** 清空会话。 |
| **`SessionActivityManager`** | **已实现**（`src/mm1/managers/SessionActivityManager.cpp`）：**`TouchSession` / `CleanupExpiredSessions`** 等委托 **`MM2::Mm1*ImSessionActivity*`** → **`mm1_im_session_activity`**（**`user_version=11`**；**进程重启可恢复**，须 **`MM2::Initialize`**）。 |
| **`MessageReplyManager`** | **已实现**（`src/mm1/managers/MessageReplyManager.cpp`）：**`callerSessionId`** → **`TryGetSessionUserId`**，principal **必须**与 **`senderId`** 一致（**`common::Memory::ConstantTimeCompare`**）；**`senderEd25519PublicKey`（32B）** + **canonical payload** 上 **Ed25519 验签**；通过后 **`mm2::MM2::StoreMessageReplyRelation`**。**群会话**：**`MM2`** 内若 **`imSessionId`** 在 **`group_members`** 有行，则 **SQL** 校验 **回复作者** 与 **`repliedSenderId`** 均为成员；**单聊**（无群行）不触发该校验。 |
| **`MessageRecallManager`** | **已实现**（`MessageRecallManager.cpp`）：**Ed25519** 验签（canonical **`ZChatIM|RecallMessage|v1`**）+ **`UserDataManager`** 中 **`senderId`→32B 公钥**（类型常量见源码）；通过后 **`MM2::DeleteMessage`**。 |
| **`UserDataManager`** | **已实现**（**`UserDataManager.cpp`** + **pimpl**）：**`MM2::Initialize` 后**经 **`MM2::StoreMm1UserDataBlob` 等** 写入元库表 **`mm1_user_kv`**（**持久化**）；未初始化 MM2 时回退进程内内存表。供 **Recall 公钥**、**JNI `StoreUserData`** 等。 |
| **`FriendManager` / `FriendVerificationManager`** | **已实现**（**`FriendManager.cpp` / `FriendVerificationManager.cpp`**）：**Ed25519** 验签（**canonical v1** 见 **`docs/04-Features/05-FriendVerify.md`** 附录）+ **`UserData`** 公钥（**`0x45444A31`**）+ **`MM2::friend_requests`**；**`GetFriends`** / **`DeleteFriend`** 基于 **status=1** 边（**无单独 friends 表**）。 |
| **`GroupManager`** | **已实现**（**`GroupManager.cpp`**）：**`Crypto::GenerateSecureRandom(MESSAGE_ID_SIZE)`** 新 **groupId**；**`MM2::CreateGroupSeedForMm1`** → **`group_members`**（**owner**）+ **`mm2_group_display`** + **首包 `ZGK1`**（**`group_data` + `.zdb`**）；**邀请**须 **`ListAcceptedFriendPeerUserIds(inviter)`** 含被邀请人且 **admin/owner**；**踢人**须 **owner**；**`UpdateGroupKey`**：**owner/admin** → **`UpsertGroupKeyEnvelopeForMm1`** 轮换 **`ZGK1`**；**`GetGroupMemberExistsForMm1`** 等见前文；**role** 0/1/2；**群名 ≤2048**；**JNI `principal`** 语义见 **`01-JNI.md`**。 |
| **`GroupNameManager`** | **已实现**（**`GroupNameManager.cpp`**）：**`UpdateGroupName`** — **owner/admin** + **非空名 ≤2048 UTF-8**；**`nowMs`** → **`updated_s`**（**`nowMs/1000`**）；**`MM2::UpdateGroupName`**。**`getGroupName`** 仍 **`JniBridge` → `MM2::GetGroupName`**（仅 **caller**）。 |
| **`GroupMuteManager`** | **已实现**（**`GroupMuteManager.cpp`**）：**`mm2_group_mute`**（表自 **v6**；元库正常 **`user_version=11`**）；**禁言/解禁**须 **owner/admin** 且在群；**admin** 仅可禁 **member**；**不可**禁 **owner**、**不可自禁**；**`duration_s=-1`** 永久；**`CleanupExpiredMutes`** / **`MM2::CleanupExpiredData`** 删到期行；**`MM2::DeleteGroupMemberForMm1`** 会删对应禁言行。 |
| **`MessageEditManager` / `MessageEditOrchestration`** | **已实现**（**`MessageEditManager.cpp`**）：**Ed25519** 验签（**`ZChatIM|EditMessage|v1`** ‖ **`message_id`‖`sender_id`‖`editTimestamp` u64BE‖** **`SHA-256(newEncryptedContent)`**）；**`UserData` 公钥 `0x45444A31`**；**`edit_count<3`**、**相邻编辑间隔 ≤300s**（秒）；**`MM2::EditMessage`**。**`CheckEditAllowed`** 仅做状态/时间窗（**不**验签，见源码注释）。 |
| **`MentionPermissionManager`** | **已实现**（**`MentionPermissionManager.cpp`**）：验签（**`ZChatIM|MentionRequest|v1`** ‖ **`groupId`‖`senderId`‖`mentionType` i32BE‖`nowMs` u64BE‖`mentionedUserIds`…**）；**mentionType=1** 校验 **@用户** 均在群（**`MM2` → `group_members` SQL**）；**=2 @ALL** 须 **owner/admin**（**SQL 角色**），且 **60s** 滑动窗内 **≤3 次**（**`mm1_mention_atall_window`** + **`RecordMentionAtAllUsage`**；**重启可恢复**）。 |
| **`FriendNoteManager`** | **已实现**（**`FriendNoteManager.cpp`**）：验签（**`ZChatIM|UpdateFriendNote|v1`** ‖ **`userId`‖`friendId`‖`updateTimestamp` u64BE‖** **`SHA-256(note)`**）；须 **accepted 好友**；**`mm1_user_kv`** **`type=0x464E424E`（'FNBN'）** 打包多好友备注（**`ZFN1`** 前缀，见源码）。单条备注 **≤64KiB**。 |
| **`AccountDeleteManager`** | **已实现**（**`AccountDeleteManager.cpp`**）：**`reauthToken` 与 `secondConfirmToken` 须同长 ≥16B 且逐字节相同**；写 **`mm1_user_kv` `type=0x41434431`（'ACD1'）** 墓碑；**不**自动 **`CleanupAllData`**。 |
| **`MM1_manager_stubs.cpp`** | **`SystemControl`**：**`EmergencyWipe` → `MM1::EmergencyTrustedZoneWipe`**；**`GetStatus` / `RotateKeys`** 同上。**`DeviceSessionManager`** / **`CertPinningManager`** / **`UserStatusManager`**：委托 **`MM2` → `SqliteMetadataDb`**（**`mm1_device_sessions` / `mm1_cert_pin_*` / `mm1_user_status`**），**进程重启可恢复**（须 **`MM2::Initialize`**；**在线态**为**最后已知**缓存，**服务端**权威）。**`SessionActivityManager`**：**`mm1_im_session_activity`** 持久化。 |
| **`JniBridge` / `JniInterface`** | **已实现**（**`src/jni/JniBridge.cpp`**、**`JniInterface.cpp`**）：**`Initialize(dataDir,indexDir)`**、会话绑定、**MM2/MM1** 路由（见 **`01-JNI.md`**）。 |
| **`MM1.h` / `MM1.cpp`** | **`Initialize`** / **`Cleanup`** / 内存与密钥门面同前；**`ValidateJniCall`/`JniStringToString`/`JniByteArrayToVector`** → **`JniSecurity`**（**`ZCHATIM_HAVE_JNI`** 时 **UTF-8 / jbyteArray** 等真实转换；否则空/指针守卫）；**`IsDebuggerPresent`/`EnableAntiDebug`** → **`AntiDebug`**（**Windows**：**`::IsDebuggerPresent`**）。**`Get*()`** 引用并发注意同前。 |
| **`MM1_security_submodules.cpp`** | **`SecurityMemory`**、**`MemoryEncryption`**、**`KeyManagement`**、**`SecureRandom`**、**`SideChannel`**（**延时/缓存类 API** 仍多为空操作；**`ConstantTimeCompare(uint64_t)`** 委托 **`common::Memory`**）；**`AntiDebug::IsDebuggerPresent`**（Win）；**`JniSecurity`**：**`ZCHATIM_HAVE_JNI`** 时 **String/ByteArray/Exception/Integer/Long** 等；**`AllocateJniMemory`/`FreeJniMemory`** 委托 **`common::Memory`**（**`SecureZero` 后返回**）。 |
| **`common/Ed25519.{h,cpp}`** | **`Ed25519VerifyDetached`**：**OpenSSL 3** **EVP_PKEY_ED25519**。 |
| **其余 `include/mm1/*.h`（大量 Manager / 安全子模块头文件）** | **`SystemControl`** 与 **`DeviceSessionManager` 等** 同在 **`MM1_manager_stubs.cpp`**（见上行）；**JNI** 路由仍走 **`JniBridge`**。与 **`01-MM1.md`** 双棘轮等叙述相比，**安全子模块**仍有部分桩。 |

---

## 4. JNI 与公共头

**caller / principal 绑定**：以 **`docs/06-Appendix/01-JNI.md`「principal 绑定矩阵」** 为权威，与 **`src/jni/JniBridge.cpp`** 严格一致（**非**「凡 API 均 `userId==principal`」）。

| 区域 | 状态 |
|------|------|
| **`jni/ZChatIMJNI.cpp` + `jni/JniNatives.cpp`** | **`JNI_OnLoad`** 注册 **`com.yhj.zchat.jni.ZChatIMNative`** 全部 **`native`** → **`JniInterface`**（与 **`01-JNI.md`** / **`JniInterface.h`** 同序）。 |
| **`include/jni/*.h` + `src/jni/*.cpp`** | **`JniBridge`/`JniInterface` 已实现**；Java 对照 **`ZChatServer/.../ZChatIMNative.java`**。 |
| **`include/common/JniSecurityPolicy.h`** | 策略常量/约定；**`mm2/MM2.h` 已包含**；**与 native 实现同步靠人工**。MM2 与其余 **`common/*` 工具**关系见 **第2.4节**。 |
| **`include/common/{Utils,Memory,String,File,Time,Random}.h`** | 对应 **`src/common/{Utils,Memory,String,File,Time,Random}.cpp`**，命名空间 **`ZChatIM::common`**（与 **`Logger`** 分列，均上表 **`ZChatIMCore`**）。**`Memory`**：匿名命名空间内 **`std::atomic<size_t>`** 的 **`g_allocatedSize` / `g_peakMemoryUsage`**；**`GetAllocatedSize`/`GetPeakMemoryUsage`/`ResetMemoryStats`** 与 **`Allocate`/`Free`/`Reallocate`** 一致；**`AllocateAligned`/`FreeAligned`** **不计入**上述统计（见 **`Memory.h`** 注释）。**`Random.cpp`**：**`ReadOsRandom`** → **`RAND_bytes`**；**Unix** **`g_urandomMutex` + `g_urandomFd`** 读 **`/dev/urandom`**；**`g_rngSeeded`（`std::atomic<bool>`）+ `g_rngMutex` + `g_mt`（`mt19937`）** 惰性播种（**无** `Random` 类静态 **`s_initialized`**；**勿与** **`ZChatIM::mm2::Crypto::s_initialized`** 混淆）。**`GenerateBytes`/`GenerateSecureBytes`/`GenerateRandomString`** 对 **`length`** 有**实现上限**（防 OOM，见 **`Random.h`** / **`Random.cpp`**）。**`GenerateBytes`/`GenerateInt`/`GenerateUInt`/`GenerateBool`**：**`mt19937`**（**非** CSPRNG）。**`GenerateSecureBytes`** / **`GenerateMessageId`** / **`GenerateSessionId`**：仅 **`ReadOsRandom`**；失败返回**空向量**。**`GenerateSecureInt`/`GenerateSecureUInt`**：**`ReadUniformIndex`**；失败返回 **`min`**。**`GenerateFileId`/`GenerateRandomString`**：**`FillStringUniformAlpha62`** 仅用 **`ReadUniformIndex`**；失败返回**空串**（**不**回退 **`uniform_int_distribution` + `g_mt`**）。**`File::ReadFile`** 校验 **`read` 完整读满**；**`WriteFile`/`AppendFile`** 拒绝 **`length` 超过 `std::streamsize` 可表示范围**。**`File::ListDirectory(path, out, ec)`**：**`directory_iterator` + `increment(ec)`**；**`true` 且 `ec` 清零**＝成功（**`out` 空**＝空目录）；**`false`**＝打开或遍历失败。 |
| **`mm2/storage/Crypto.cpp`**（**`Crypto::Init`/`Cleanup`**） | **`g_cryptoInitMutex`**：**`Init` 双检 + 互斥**、**`Cleanup` 同锁**，与 MM2 单线程 API 形成**纵深防御**；**`EncryptMessage`/`DecryptMessage`（Windows）** 在 **`plaintextLen`/`ciphertextLen` 超过 `ULONG` 可表示范围** 时 **`false`**。**`ReadDevUrandom`（非 Windows）** 校验 **`read` 读满** 且 **`len` 不超过 `std::streamsize` 上限**。 |
| **`Logger.h`**（`include/` 根） | **实现** **`src/common/Logger.cpp`**（命名空间 **`ZChatIM`**），已编入 **`ZChatIMCore`**。**`m_logLevel`**：**`std::atomic`**；**`m_logFile`** 与 **`Log*`** 共用 **`mutex`**（含 **`SetLogFile` / `CloseLogFile` / 析构**）。**`SetLogFile`**：**`filePath` 为空** 时 **`false`**。 |

**边界健壮性（与 `Utils.h` / `Memory.h` 注释及实现对齐）**：**`Utils`** — **`length>0` 且 `data==nullptr`** 时 **`BytesToHex`/`BytesToString` 返回空串**；**`BytesToHex`** 在 **`length*2` 溢出 `size_t`** 时返回**空串**；**`BytesToString`** 在 **`length` 超过 `std::string::max_size()`** 时返回**空串**；**`HexToBytes`** 在**需写出字节数 `>0` 且 `output==nullptr`** 时 **`false`**；**`CalculateCRC32`/`CalculateAdler32`** 在 **`length>0` 且 `data==nullptr`** 时返回 **0**（**合法空缓冲的 Adler32 为 1**）。**`Memory`** — **`Allocate`/`Reallocate`** 在 **`size`（或 `newSize`）+ `sizeof(size_t)` 头** **`size_t` 溢出** 时 **`nullptr`**（**`Reallocate` 失败时原块不变**）。

---

## 5. 头文件目录说明（与 `#include` 一致）

- **`include/mm2/MM2.h`**：编排入口。
- **`include/mm2/storage/*.h`**、**`include/mm2/crypto/*.h`**：存储与哈希实现头文件。
- **已删除**：**`include/mm2/`** 下曾存在的 **`storage/` 同名转发头**（见 **第7.4节**）。**勿**再引入第三套别名路径。
- **包含根目录**：**`include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)`**（全局）与 **`ZChatIMCore` `target_include_directories(PUBLIC .../include)`**；依赖 **`ZChatIMCore`** 的目标继承 **`PUBLIC`** 接口包含路径。以源码 **`#include "mm2/..."` / `"common/..."`** 为准。

---

## 6. 本地数据目录（部署注意）

| 项 | 说明 |
|----|------|
| **非**上线布局 | 生产环境应通过配置传入 **`MM2::Initialize(dataDir, indexDir)`**，勿依赖构建输出目录下的任意临时路径。 |

---

## 7. 任务调整与优先级（可勾选）

> **维护**：合并/完成某项后改勾选与日期备注；**详细行为**仍以 第2节、第8节 与 **`03-Storage.md` 第七节** 为准。

### 7.1 当前迭代（进行中）

- **M1 集成联测**：**阻塞** — **Android 客户端**与 **Spring Boot** **未就绪**，**暂不**做端到端联测；**回归**以 **`ZChatIM --test`** 为准。解禁后按 **`docs/07-Engineering/02-Cpp-Completion-Roadmap.md` 第3节** 补「产品场景 + 含重启」验收。
- **M2 最小落地**：**不依赖联测**；对照 **`docs/07-Engineering/03-M2-Minimal-ServerOwnedDevices.md`** + **`03-Storage.md` 第4.2 / 4.3节**（服务端设备权威、本地元表语义）。
- **M3（双棘轮等愿景）**：**独立立项**，范围见 **`01-MM1.md` 一点五.3** 与 **`02-Cpp-Completion-Roadmap.md` 第5节**；**非**本迭代交付义务。

### 7.2 P0 — 功能闭环（近期）

- [x] **`MM2::StoreMessages` / `RetrieveMessages` / `GetSessionMessages`**（**IM 索引 RAM**；**`Initialize`** 仅 **`ImRamClearUnlocked()`**，**不**扫盘清历史 IM）。  
- [x] **`MessageQueryManager::ListMessages*`** 已委托 **`MM2`**。  
- [x] **`MarkMessageRead` / `GetUnreadSessionMessages`**（**RAM** **`has_read` / `read_at_ms`**；与历史 **`im_messages.read_at_ms`** 语义一致）。JNI **`getUnreadSessionMessageIds`** 已由 **`jni/JniNatives.cpp`** 接线至 **`JniBridge`**。
- [x] **MM2 余量 API**：**IM 侧**（回复/编辑、**`ListMessagesSinceTimestamp`**、**`CleanupSessionMessages`**、**`GetMessageCount`**）走 **RAM**；**好友/群/文件/续传等**仍接 **`SqliteMetadataDb`**（**`user_version=11`**，**无** IM 表）。**群会话回复**：**`StoreMessageReplyRelation`** 经 **`group_members`** **SQL** 校验成员（与 **`05` 第3节 `MessageReplyManager`** 一致）。  
- [x] **`ZChatIMJNI`**：**`RegisterNatives`** 全表（**`jni/JniNatives.cpp`**）+ **`ZChatIMNative.java`**；**`getSessionMessages` / `listMessages*`** 的 Java **`byte[][]`** 行格式与 **`MessageQueryManager.h`** / **`01-JNI.md`**（**`message_id(16)‖lenBE32‖payload`**）一致。

### 7.3 P1 — 安全与上线相关

**服务端管多设备**时的 **M2 最小落地顺序**：**`docs/07-Engineering/03-M2-Minimal-ServerOwnedDevices.md`**。

- [x] **M2「增强」文档结案**：**HSM / libsecret（或 Credential Manager）/ MM1 密钥策略大一统** 等按 **`docs/07-Engineering/04-M2-Key-Policy-And-Extensions.md` 第3节、第6节** 列为**非本期实现**；合规需要时 **单独立项** 并回写 **`04`/`05`**。**本期交付**以 **ZMK1/2/3 + ZMKP + SQLCipher 派生链** 为界。

- [x] **`mm2_message_key.bin` 最小闭环（服务端管多设备）**：**ZMK1 / ZMK2 / ZMK3** 已实现（**`MM2.cpp`**、**`MM2_message_key_darwin.cpp`**）；运维与 **`Cleanup` vs `CleanupAllData` / 紧急擦除** 口径见 **`03-Storage.md` 第4.3节**；清单与勾选见 **`docs/07-Engineering/03-M2-Minimal-ServerOwnedDevices.md`**。**回归**：**`ZChatIM --test`**（含 **`RunMm2MessageKeyProtectedFileTests`**、**`CleanupAllData`**、Win **明文→ZMK1 迁移** 等）。  
- [x] **`mm2_message_key.bin` 增强（按产品）**：**ZMKP v1**（口令 + PBKDF2 + GCM 包裹主密钥）、**`MM2::Initialize(..., passphrase)`** 已落地；**JNI `initializeWithPassphrase`** 由 native 桥接（**`JniBridge` / `JniNatives`** 等）对齐 C++（Java 侧以 **`01-JNI.md`** 为准）。**HSM** / **libsecret** / **与 MM1 统一密钥策略** 的**可选扩展**见 **`docs/07-Engineering/04-M2-Key-Policy-And-Extensions.md`**（**明确不做**清单）。**SQLCipher** 仍从该主密钥 **域分离派生**（**`03-Storage.md` 第4.2节**）。**`--test`**：**`RunMm2MessageKeyProtectedFileTests`** 含 **ZMKP** 子用例（**`ZCHATIM_USE_SQLCIPHER=ON`**）。  
- [x] **SQLCipher**：**`ZCHATIM_USE_SQLCIPHER`**（默认 ON）、**`SqliteMetadataDb::Open(…, key)`**、**明文库迁移**、**第4.2节 PRAGMA**（见 **`03-Storage.md`**）。  
- [x] **`StoreMessages` 批处理语义**：失败时对已成功条**内部回滚**、**`outMessageIds` 清空**（**全有或全无**；**`LastError`** 为首条失败原因）。

### 7.4 P2 — 工程与 MM1

- [x] 收敛 **`include/mm2/...` 重复路径**：已删除 **`mm2/*.h` 转发层**，统一 **`mm2/storage/...`**（见 **第5节**）。  
- [x] **MM1 P0（内存/RNG/密钥门面）**：**`AllocateSecureMemory`** 等与 **`common::Memory` / `mm2::Crypto`** 对齐（见 **第3节**）。  
- [x] **MM1 并发契约**：**`MM1`** public 实例方法入口持 **`m_apiRecursiveMutex`**；**`JniSecurityPolicy.h`** 补充 **初始化顺序**、**MM1→MM2 锁顺序** 与 **`JniBridge::m_initialized`（`std::atomic<bool>`）**。**`MM1::Get*Manager()`** 仅覆盖**取引用**瞬间；**JNI** 整段调用仍受 **`JniBridge::m_apiRecursiveMutex`** 保护（**细目**：**`ZChatIM/docs/JNI-API-Documentation.md` §0 第7条**）。  
- [x] **`MessageReplyManager` → MM2`**：**`StoreMessageReplyRelation`** 含 **`TryGetSessionUserId`**、**Ed25519**、**MM2** **RAM** 存回复关系；**群会话**另经 **`group_members`** **SQL** 成员校验（**`user_version=11`**）。  
- [x] **`ZChatIMJNI` + `JniBridge`**：**`RegisterNatives`** 全量绑定（**`jni/JniNatives.cpp`**）。  
- [x] **`01-MM1.md` 愿景（M3）**：**双棘轮、纯内存分散消息、Random Allocator 等** 在 **`01-MM1.md` 一点五、一点五.3** 与 **[02-Cpp-Completion-Roadmap.md](../07-Engineering/02-Cpp-Completion-Roadmap.md) 第5节** 已**结案**为 **M3 独立立项、非当前 C++ 交付义务**（**不**占 **`--test`** / 发版阻塞项）；**当前默认**以 **`05` 第2～3节** 与 **`ZChatIM/` 源码**为准。**各 Manager 产品级业务链**仍可能扩展（**JNI 入口已接**）。**`UserStatusManager`**（**`mm1_user_status`**）与 **多设备 / IM 活跃 / 证书 Pin / @ALL 限速窗** 已落 **元库**（见 **第3节 `MM1_manager_stubs.cpp` / `MentionPermissionManager.cpp`**）。

---

## 8. 风险与部分失败路径（稳定性彻查摘要）

| 风险 | 说明 / 缓解 |
|------|-------------|
| **无跨存储事务** | **`.zdb` 追加/覆盖** 与 **SQLite** 分步提交。块写入在 **`WriteData` 成功后** 多数元数据失败会 **`RevertFailedPutDataBlockUnlocked`**；**覆盖先删旧块** 等路径仍可能不一致（**`03-Storage.md` 第七节**）。**IM `DeleteMessage`** 仅 **RAM**，不涉及 **`.zdb`/SQLite** 原子删行。 |
| **`PutDataBlockBlob` / `StoreFileChunk`** | **`WriteData` 成功后** 若 **`GetFileStatus` / `UpsertZdbFile` / `ComputeSha256` / `RecordDataBlockHash`** 失败则调用 **`RevertFailedPutDataBlockUnlocked`**；**`LastError`** 仍为主因（补偿失败时 **`revert:`** 前缀见实现）。 |
| **`StoreMessage`** | **仅 RAM**：密文 blob + 会话索引；**`Initialize`** **不**清理历史磁盘 IM（**`ImRamClearUnlocked` 仅清空本进程 RAM**）。**`LastError`** 见各校验/加密失败路径。 |
| **`StoreMessages`** | 顺序 **`StoreMessage`**；任一条失败则 **`ImRamEraseUnlocked`** 回滚本批已成功条、**`outMessageIds` 清空**（**全有或全无**）。 |
| **`GetSessionMessages` / `RetrieveMessages`** | 逐条 **`RetrieveMessage`**；**任一条失败**则清空对应输出并 **`false`**（**全有或全无**）。 |
| **`MessageQueryManager::List*`** | **`owner_==nullptr`** 时返回空且**不**改 **`LastError`**。**`ListMessagesSinceTimestamp`**：**`count<=0`** 时空；**`count>0`** 时须 **`Initialize`**、**`sessionId` 16B**、**`EnsureMessageCryptoReadyUnlocked`**，在 **RAM IM** 上按 **`stored_at_ms >= sinceTimestampMs`** 取 **`count`** 条。 |
| **`MarkMessageRead` / `GetUnreadSessionMessages`** | **仅 RAM IM 行**。**`DeleteMessage` / `ImRamEraseUnlocked`** 时一并删回复映射。**`readTimestampMs`** 须 **≤ `int64_t` 最大**（否则 MM2 拒绝）。 |
| **`DeleteMessage`（IM）** | **`ImRamEraseUnlocked`**：**不**动 **`StoreFileChunk`** 所用 **`data_blocks`**。 |
| **`DeleteData` 与空间统计** | **不降低** `ZdbHeader.usedSize`（洞在中间）；**`zdb_files.used_size`** 与实现写入路径一致，**不保证**随洞增加而减小。 |
| **并发** | **`MM2`** 实例方法入口持 **`m_stateMutex`（`std::recursive_mutex`）**。**`SqliteMetadataDb::Open`** 使用 **`sqlite3_open_v2`** 标志 **`READWRITE` + `CREATE` + `FULLMUTEX`**（**`SqliteMetadataDb.cpp`**）；**`ZCHATIM_USE_SQLCIPHER`** 时另有 **`sqlite3_key_v2`** 与 **第4.2节 PRAGMA**。**编排契约**：**仅**经 **`MM2`** 访问同一 **`SqliteMetadataDb`**；**勿**在其它线程绕过 **`m_stateMutex`** 对该连接做并发 API。**勿**在已持 **`m_stateMutex`** 的 **`MM2` 内部路径**中重入 **`MM2` 公开 API**（死锁）。 |
| **`ZChatIM::common` / `Logger`** | **`Memory` / `Random` / `Logger`** 的统计与 RNG、日志 I/O 约定见 **第4节**。**`File` / `Utils` / `String` / `Time`** 无进程内全局锁；**同一文件路径**上的并发写或与外部删改交错，须由**调用方**保证与 **OS** 语义一致。 |
| **`ZdbManager` 与 `MM2` 子对象** | **`GetStorageIntegrityManager()`** 等返回引用不持锁，调用须与 MM2 **串行**（**`MM2.h`** / **`JniSecurityPolicy.h`**）。**`MessageQueryManager::List*`** 经 MM2 内部回调已持 **`m_stateMutex`**，可与其它 MM2 API 安全交错。**`DeleteFile`**：**`fileId` 须非空且通过 `IsSafeZdbFileId`**（与 **`OpenFile`/`ReadData`** 一致），防路径逃逸。**`ZdbFile::WriteData`/`AppendRaw`/`ReadData`**：拒绝 **`length` 超过 `std::streamsize` 可表示**。**`OverwriteData`**：按 **`FILE_CHUNK_SIZE` 分块写零**，避免单次巨量 **`std::vector` 分配**。 |
| **`Crypto` 进程内静态** | **`Crypto::Init/Cleanup`**：**OpenSSL** 路径下主要为 **`s_initialized`** 标志位。当前 **MM2 单例** 与 **`Cleanup` 成对**即可。 |
| **构建依赖** | **全平台 OpenSSL 3**（**`OpenSSL::Crypto`**；**SQLCipher** 另 **`OpenSSL::SSL`**）。**Windows** **`crypt32`**（**DPAPI**）。**OpenSSL 安装树**：**`prebuilt/windows-x64/openssl/`** 等（**`thirdparty/openssl/LAYOUT.md`**）或 **`OPENSSL_ROOT_DIR`**。**`-DZCHATIM_USE_SQLCIPHER=OFF`** 仍要 OpenSSL；仅元数据改明文 **sqlite3**。**勿需** **`libsqlcipher-dev`**。 |
| **JNI** | **已接**（**`JniBridge` 持锁 + MM2 `m_stateMutex`**）；Java 线程仍须**避免**在应用层对同一 **`MM2` 实例**做无锁并发 native 调用；遵守 **`JniSecurityPolicy`**。 |

---

## 9. 相关文档

| 文档 | 用途 |
|------|------|
| [README.md](../README.md) | **冲突与权威**（落盘 vs 内存、重启语义） |
| [03-Storage.md](03-Storage.md) | 元数据表、MM2 行为、实现落点 第七节 |
| [04-ZdbBinaryLayout.md](04-ZdbBinaryLayout.md) | `.zdb` v1 二进制 |
| [02-MM2.md](02-MM2.md) | 架构与产品叙述（与 第七节 冲突时以 第七节 + 源码为准） |
| [01-MM1.md](01-MM1.md) | MM1 安全框架叙述 |
| [01-JNI.md](../06-Appendix/01-JNI.md) | JNI 方法表（与 `JniInterface.h` 严格对应） |
| [01-Build-ZChatIM.md](../07-Engineering/01-Build-ZChatIM.md) | **`ZChatIMCore` / EXE / JNI** 目标与 **`CMakeLists.txt`** 选项 |
| [02-Cpp-Completion-Roadmap.md](../07-Engineering/02-Cpp-Completion-Roadmap.md) | C++ **M0–M3** 阶段划分 |
| [03-M2-Minimal-ServerOwnedDevices.md](../07-Engineering/03-M2-Minimal-ServerOwnedDevices.md) | **M2 最小清单**（**服务端管多设备**）与收尾状态 |
| `ZChatIM/docs/JNI-API-Documentation.md` | JNI 详细路由与安全不变量 |
| [02-ZSP-Protocol.md](../01-Architecture/02-ZSP-Protocol.md) | ZSP 消息类型与 TLV 扩展（与功能文档交叉引用） |
