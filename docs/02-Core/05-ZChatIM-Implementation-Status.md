# ZChatIM C++ 实现状态（活文档）

**作用**：对照 **`ZChatIM/include/`** 与 **`ZChatIM/src/`**，记录「已实现 / 桩 / 未接」，避免头文件扩张与文档漂移。  
**维护**：完成功能或删改 API 时顺手改本页对应行；详细行为仍以 **`03-Storage.md`**、**`04-ZdbBinaryLayout.md`**、**`MM2.h`** 与源码为准。  
**文档冲突**：凡 **`04-Features` / 总览** 与「消息是否落盘、重启是否丢」矛盾时，**先读** **`docs/README.md`**「**冲突与权威**」，**再以本页 + 第8节 为准**。

### 如何阅读（约 1 分钟）

1. **存储与 `.zdb`**：看 **第2.1节 容器 / 元数据** + **`03-Storage.md` 第七节** + **`04-ZdbBinaryLayout.md`**。  
2. **哪些 API 已能调用**：看 **第2.1节 编排**；**MM2 行为细则与边界**：看 **第2.2节**；**JNI 未接 / MM1 大块未实现**：看 **第4节**、**第7节**。  
3. **失败与并发**：看 **第8节**（部分失败路径、锁、`MessageQueryManager`）。  
4. **JNI / Java**：**第4节**（当前仅桩）；契约以 **`docs/06-Appendix/01-JNI.md`** + **`JniInterface.h`** 为准。  
5. **下一步做啥**：**第7节 勾选清单**。

---

## 1. 构建产物

| 目标 | 说明 |
|------|------|
| **`ZChatIMCore`**（静态库） | 当前所有已编译 C++ 实现均汇入此库。 |
| **`ZChatIM.exe`** | `main.cpp`（可选链接本地开发用例源文件时，可保留命令行入口；**规范文档不描述**）。 |
| **`ZChatIMJNI`**（可选） | `jni/ZChatIMJNI.cpp`：**仅 `JNI_OnLoad` / `JNI_OnUnload` 桩**，无业务 native 导出。 |

---

## 2. MM2（消息与存储）

### 2.1 已有 `.cpp` 且与文档对齐度较高

| 组件 | 头文件 | 源文件 | 状态 |
|------|--------|--------|------|
| 编排 | `mm2/MM2.h` | `MM2.cpp` | **核心路径**：`Initialize`（末尾 **`MessageQueryManager::SetOwner(this)`**）、`StoreFileChunk`/`GetFileChunk`、**`CompleteFile`/`CancelFile`/续传索引**、`Cleanup`/`CleanupAllData`、`GetStorageStatus`；**`StoreMessage`/`StoreMessages`/`RetrieveMessage`/`RetrieveMessages`/`GetSessionMessages`/`DeleteMessage`** 与 **`GetMessageQueryManager()::ListMessages*`**（**AES-GCM**；**Windows BCrypt** / **Unix OpenSSL 3**）；**`MarkMessageRead`/`GetUnreadSessionMessages`**；**`StoreMessageReplyRelation`/`GetMessageReplyRelation`**、**`EditMessage`/`GetMessageEditState`**、**`CleanupSessionMessages`**；**`StoreFriendRequest`/`UpdateFriendRequestStatus`/`DeleteFriendRequest`/`CleanupExpiredFriendRequests`**；**`UpdateGroupName`/`GetGroupName`**；**`CleanupExpiredData`**（当前主要清过期 **pending** 好友请求）、**`OptimizeStorage`**（**`VACUUM`**）；**`GetMessageCount`**→**`im_messages` 行数**；**懒加载** `Crypto` + `mm2_message_key.bin`（见 **`03-Storage.md` 第七节**）。 |
| 元数据 | `mm2/storage/SqliteMetadataDb.h` | `SqliteMetadataDb.cpp` | **完成**：schema **`user_version=4`**（**`im_messages`**：**`read_at_ms`**、**`edit_count`/`last_edit_time_s`**；**`im_message_reply`/`friend_requests`/`mm2_group_display`/`mm2_file_transfer`**；旧库 **`ALTER TABLE`** 迁移）。 |
| 完整性 | `mm2/storage/StorageIntegrityManager.h` | `StorageIntegrityManager.cpp` | **完成**：SHA-256 + 与 SQLite 联动。 |
| 容器 | `mm2/storage/ZdbFile.h`、`ZdbManager.h` | `ZdbFile.cpp`、`ZdbManager.cpp` | **完成**：v1 布局、**`Create` 随机预填 payload**、`AppendRaw`/读写/删除等（见 **`04-ZdbBinaryLayout.md`**）。 |
| 加密 | `mm2/storage/Crypto.h` | `Crypto.cpp` | **Windows**：**BCrypt**（AES-GCM、PBKDF2、RNG + **CryptoAPI** 后备）。**Linux/macOS**：**OpenSSL 3**（同上语义）。**`Encrypt* / Decrypt* / DeriveKey`** 须 **`Init`**；**`GenerateSecureRandom` / `HashSha256`** 不依赖 **`s_initialized`**。 |
| 哈希 | `mm2/crypto/Sha256.h` | `Sha256.cpp` | **完成**。 |
| 其它存储辅助 | `BlockIndex.h`、`MessageQueryManager.h` | `BlockIndex.cpp`、`MessageQueryManager.cpp` | **`MessageQueryManager`** 已接 **`MM2`**（**`SetOwner`** 于 **`Initialize`/`CleanupUnlocked`**）：**`ListMessages`** → **`GetSessionMessages`**；**`ListMessagesSinceMessageId`** → **`ListImMessageIdsForSessionChronological` / `ListImMessageIdsForSessionAfterMessageId`** + **`RetrieveMessage`**；**`ListMessagesSinceTimestamp`** 仍**不支持**（**`im_messages` 无时间列**，**`LastError`** 说明）。返回行编码见 **`MessageQueryManager.h`**。 |

### 2.2 `MM2` 扩展语义备忘（非「未实现」清单）

- **`CompleteFile`**：按 **连续** chunk 索引 **0..N-1** 读取并计算 **SHA-256**（流式 **`crypto::Sha256Hasher`**），与传入摘要一致后写入 **`mm2_file_transfer`**（**`ON CONFLICT`** 可补行）。若 chunk 序列有**空洞**，校验以**首个缺失**为界，可能不符合预期，产品层应保证顺序写满。**`Sha256Hasher` 位计数为 64 位**：输入总长 **≥ 2⁶³ 字节** 时理论溢出（常规模型下可忽略）。
- **`CleanupExpiredFriendRequests` / `CleanupExpiredData`**：当前策略为删除 **`status=0`（pending）** 且 **`created_s` 早于 `now - 30 天`** 的行（秒级时间轴）；可按产品再调 **`ttl`** 或扩展其它表。
- **`EditMessage`**：**不**重算 **`StoreMessage` 的 AES-GCM**；**`newEncryptedContent`** 为**已加密包**（与 **`data_blocks` chunk0** 同上限），**`edit_count`** 须在 **[1,3]**（上层规则仍以 MM1/JNI 为准）。
- **`GetTransferResumeChunkIndex`**：无 **`mm2_file_transfer`** 行时返回 **`false`**（与 JNI 侧 **`UINT32_MAX` 哨兵** 接线时须转换）。

### 2.3 已知限制（与 **`03-Storage.md` 第七节** 一致）

- v1 **无跨 `.zdb` 与 SQLite 的单事务**；**`PutDataBlockBlob` / `StoreFileChunk`** 在 **`WriteData` 成功后** 若元数据链失败会尽力 **`RevertFailedPutDataBlockUnlocked`**；**`StoreMessage`** 在 **`InsertImMessage` 失败**后另有补偿。**覆盖写**在 **`Record` 前已 `DeleteData` 清零旧块**等路径仍可能产生**不可自恢复**的不一致，见 **第8节**。  
- 更细的**部分失败形态**见 **第8节（风险与部分失败路径）**。

---

## 3. MM1

| 组件 | 状态 |
|------|------|
| **`AuthSessionManager`** | **已实现**（`src/mm1/managers/AuthSessionManager.cpp`）。 |
| **`SessionActivityManager`** | **已实现**（`src/mm1/managers/SessionActivityManager.cpp`）。 |
| **`MM1.h` / `MM1.cpp`** | **骨架**：`Initialize`/`Cleanup` 等占位；**`AllocateSecureMemory` 等返回空或 no-op**，待接真实安全内存逻辑。 |
| **`MM1_security_submodules.cpp`** | **`mm1::security::*` 桩**，满足链接/声明，**非生产逻辑**。 |
| **其余 `include/mm1/*.h`（大量 Manager / 安全子模块头文件）** | **多数尚无对应 `.cpp`**：契约与目录结构已预留，**实现进度 = 未开始或仅通过 MM1 桩间接占位**。按需逐项接业务与 **`01-MM1.md`**。 |

---

## 4. JNI 与公共头

| 区域 | 状态 |
|------|------|
| **`jni/ZChatIMJNI.cpp`** | **桩**。 |
| **`include/jni/*.h`** | 桥接/接口声明为主；**无与 Java 的双向联调实现**。 |
| **`include/common/JniSecurityPolicy.h`** | 策略常量/约定；**与 native 实现同步靠人工**。 |
| **`include/common/{Utils,Memory,String,File,Time,Random}.h`**、`Logger.h` | **无对应 `src/common/*.cpp` 列入 `ZChatIMCore`**（若存在仅头内联或未被当前 CMake 编进库，以 **`CMakeLists.txt`** 为准）。 |

---

## 5. 头文件目录说明（技术债提示）

- 存在 **`include/mm2/...` 与 `include/mm2/storage/...` 等并行路径**（部分为历史/别名式重复）。**包含路径以 CMake `include_directories(include)` 及实际 `#include` 为准**；新代码建议优先 **`mm2/storage/...`** 与 **`mm2/MM2.h`**，避免再增加第三套路径。

---

## 6. 本地数据目录（部署注意）

| 项 | 说明 |
|----|------|
| **非**上线布局 | 生产环境应通过配置传入 **`MM2::Initialize(dataDir, indexDir)`**，勿依赖构建输出目录下的任意临时路径。 |

---

## 7. 任务调整与优先级（可勾选）

> **维护**：合并/完成某项后改勾选与日期备注；**详细行为**仍以 第2节、第8节 与 **`03-Storage.md` 第七节** 为准。

### 7.1 当前迭代（进行中）

（无固定勾选项；按版本发布前在集成环境完成验证。）

### 7.2 P0 — 功能闭环（近期）

- [x] **`MM2::StoreMessages` / `RetrieveMessages` / `GetSessionMessages`** + **`SqliteMetadataDb::ListImMessageIdsForSession`**。  
- [x] **`MessageQueryManager::ListMessages*`** 已委托 **`MM2`**。  
- [x] **`MarkMessageRead` / `GetUnreadSessionMessages`**（**`im_messages.read_at_ms`**，schema 已随 **`user_version=4`** 演进）。JNI **`GetUnreadSessionMessageIds`** 仍待 **`ZChatIMJNI`** 接线。
- [x] **MM2 余量 API**（回复/编辑、文件 **`CompleteFile`/`CancelFile`/续传、好友请求、群显示名、**`CleanupSessionMessages`/`CleanupExpiredData`/`OptimizeStorage`**、**`GetMessageCount`**）已接 **`SqliteMetadataDb` v4**。  
- [ ] 按 **`docs/06-Appendix/01-JNI.md`** 实现 **最小 native 导出** + **`ZChatIMJNI`** 注册；**`GetSessionMessages` / `ListMessages`** 的 Java 字节与 **`MessageQueryManager.h`** 行格式（**`message_id(16)‖lenBE32‖payload`**）或 **`01-JNI.md`** 表说明对齐。

### 7.3 P1 — 安全与上线相关

- [ ] **`mm2_message_key.bin` 治理（续）**：**Windows** 已实现 **ZMK1 + DPAPI**（见 **`03-Storage.md`** / **`MM2.cpp`**）；**Unix** 仍为 **32 字节明文**；**口令派生、HSM、与 SQLCipher 主密钥同源**等仍待产品与 **`Crypto` / MM2** 接口定型。  
- [ ] **SQLCipher** 或等价：与 **`03-Storage.md` 第4.2节** 对齐后改 **`SqliteMetadataDb::Open`**。  
- [x] **`StoreMessages` 批处理语义**：失败时对已成功条**内部回滚**、**`outMessageIds` 清空**（**全有或全无**；**`LastError`** 为首条失败原因）。

### 7.4 P2 — 工程与 MM1

- [ ] 收敛 **`include/mm2/...` 重复路径** 或文档明确唯一包含风格（见 **第5节**）。  
- [ ] **`MM1::AllocateSecureMemory`** 等与 **`01-MM1.md`** 对照，替换桩实现。

---

## 8. 风险与部分失败路径（稳定性彻查摘要）

| 风险 | 说明 / 缓解 |
|------|-------------|
| **无跨存储事务** | **`.zdb` 追加/覆盖** 与 **SQLite** 分步提交。块写入在 **`WriteData` 成功后** 多数元数据失败会 **`RevertFailedPutDataBlockUnlocked`**；**覆盖先删旧块**、**`DeleteMessage` 清零后 SQLite 失败** 等仍可能不一致（**`03-Storage.md` 第七节**）。 |
| **`PutDataBlockBlob` / `StoreFileChunk`** | **`WriteData` 成功后** 若 **`GetFileStatus` / `UpsertZdbFile` / `ComputeSha256` / `RecordDataBlockHash`** 失败则调用 **`RevertFailedPutDataBlockUnlocked`**；**`LastError`** 仍为主因（补偿失败时 **`revert:`** 前缀见实现）。 |
| **`StoreMessage`** | **`InsertImMessage` 失败**后尝试 **`.zdb` 补偿**（同一 **`RevertFailedPutDataBlockUnlocked`**）；成功则不再留下孤儿 **`data_blocks` 尾块**；补偿失败见 **`LastError`**。 |
| **`StoreMessages`** | 顺序 **`StoreMessage`**；任一条失败则**回滚**本批已成功写入、**`outMessageIds` 清空**（**全有或全无**；与 **`.zdb`/SQLite** 仍非单一大事务，极端下回滚单条可能失败，见 **`LastError`**）。 |
| **`GetSessionMessages` / `RetrieveMessages`** | 逐条 **`RetrieveMessage`**；**任一条失败**则清空对应输出并 **`false`**（**全有或全无**）。 |
| **`MessageQueryManager::List*`** | **`owner_==nullptr`** 时返回空且**不**改 **`LastError`**。**`ListMessagesSinceTimestamp`**：**`count<=0`** 时空且不改错；**`count>0`** 时校验 **`Initialize`** 与 **`sessionId` 16B**，再 **`LastError`** 为 **not supported**（无时间列）。 |
| **`MarkMessageRead` / `GetUnreadSessionMessages`** | 仅 **`im_messages`**；**`read_at_ms`** 与 **`.zdb` 无单事务**（与 第8节 总原则一致）。**`DeleteMessage`** 删 **`im_messages`** 行时一并清除已读态。**`readTimestampMs`** 须 **≤ `int64_t` 最大**（否则 MM2 拒绝）。 |
| **`DeleteMessage`** | 顺序：**`DeleteData`（清零）→ `DeleteMessageMetadataTransaction`（`BEGIN IMMEDIATE` 内删 `data_blocks`+`im_messages`）**。SQLite 侧**不再**出现「只删 `data_blocks` 未删 `im_messages`」或相反；**清零后事务失败**仍会 **ROLLBACK** 恢复索引行，但磁盘已为零，**SHA 校验仍可能失败**（与 v1 无跨存储事务一致）。 |
| **`DeleteData` 与空间统计** | **不降低** `ZdbHeader.usedSize`（洞在中间）；**`zdb_files.used_size`** 与实现写入路径一致，**不保证**随洞增加而减小。 |
| **并发** | **`MM2` 公开 API** 用 **`recursive_mutex`** 串行化；**`SqliteMetadataDb` 非线程安全**，须只经 MM2 访问。**勿**在持锁回调中再次进入 MM2（死锁）。 |
| **`ZdbManager` 与 `MM2` 子对象** | **`GetStorageIntegrityManager()`** 等返回引用不持锁，调用须与 MM2 **串行**（**`MM2.h`** / **`JniSecurityPolicy.h`**）。**`MessageQueryManager::List*`** 经 MM2 内部回调已持 **`m_stateMutex`**，可与其它 MM2 API 安全交错。 |
| **`Crypto` 进程内静态** | **`Crypto::Init/Cleanup`**：Windows 关闭 **BCrypt** 算法句柄；Unix 为状态位。当前 **MM2 单例** 与 **`Cleanup` 成对**即可。 |
| **构建依赖** | **Windows**：**bcrypt、advapi32、crypt32**（**DPAPI** 密钥文件），**无需** OpenSSL。**Linux/macOS**：**OpenSSL 3.x**（**`find_package(OpenSSL 3.0)`**）。 |
| **JNI** | **未接**；接 JNI 后须遵守 **`JniSecurityPolicy`** 与 MM2 锁约定，避免与 Java 线程并发打破 `SqliteMetadataDb` 假设。 |

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
| `ZChatIM/docs/JNI-API-Documentation.md` | JNI 详细路由与安全不变量 |
| [02-ZSP-Protocol.md](../01-Architecture/02-ZSP-Protocol.md) | ZSP 消息类型与 TLV 扩展（与功能文档交叉引用） |
