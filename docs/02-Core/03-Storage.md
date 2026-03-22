# 存储机制技术规范

> **文档冲突**：若其它章节写「IM 仅内存 / 重启丢消息」与本 第七节 矛盾，以 **`docs/README.md`**「**冲突与权威**」及 **`05-ZChatIM-Implementation-Status.md`** 为准。

## 一、职责

**仅作为索引和元数据存储，不存储实际业务数据。**

---

## 二、存储内容

### 2.1 文件索引

```
表: zdb_files
┌────────────┬──────────────────┬────────────┐
│ file_id    │ total_size       │ used_size  │
│ 随机文件名   │ 5242880 (5 MiB)  │ 已用空间    │
└────────────┴──────────────────┴────────────┘
```

**与实现一致**：单文件物理长度 = **`ZDB_FILE_SIZE`**（`Types.h`，当前 **5×1024×1024**）；头字段 **`totalSize` / `usedSize`** 见 **`04-ZdbBinaryLayout.md`**。

### 2.2 数据块索引

```
表: data_blocks
┌────────────┬────────┬────────┬────────┬────────┬────────┐
│ data_id    │ chunk  │ file   │ offset │ length │ sha256 │
│ 数据ID      │ 序号    │ 文件   │ 偏移    │ 长度    │ 校验值  │
└────────────┴────────┴────────┴────────┴────────┴────────┘
```

**实现约束（与 `SqliteMetadataDb` / `Types.h` 一致）**：`data_id` 为 **BLOB，长度须等于 `MESSAGE_ID_SIZE`（16 字节）**；`chunk_idx` 为 **非负** 且经绑定层须落在 **`int32_t` 可表示范围**（`StorageIntegrityManager` 要求 `chunkIndex <= INT_MAX`）；`file_id` 须 **非空** 且存在于 `zdb_files`；`sha256` 为 **32 字节 BLOB**。

### 2.3 用户数据索引

```
表: user_data
┌────────────┬────────┬────────┬────────┬────────┬─────────┐
│ user_id    │ file   │ offset │ length │ sha256 │  type   │
│ 用户ID      │ 文件   │ 偏移    │ 长度    │ 校验值  │数据类型  │
└────────────┴────────┴────────┴────────┴────────┴─────────┘

type: profile(头像), friend_list, settings
```

### 2.4 群组数据索引

**`ZGK1` 群密钥信封（MM2 当前实现）**：**`group_id`（16B BLOB）** 同时作为 **`data_blocks.data_id` 的 chunk 0** 键（与随机 **`message_id` 同长、异名使用**）。**`.zdb` 内载荷**布局：**`ZGK1`**（4 ASCII）‖ **`epoch_s` uint64 大端**（8）‖ **随机 32 字节**（**`CRYPTO_KEY_SIZE`**），共 **44** 字节。**`group_data`** 行指向该块的 **`file_id` / `offset` / `length` / `sha256`**。**`CreateGroupSeed`** 建群后即写入首包；**`UpdateGroupKey`（MM1）** 调用 **`MM2::UpsertGroupKeyEnvelopeForMm1`** 覆盖轮换。**`WriteGroupKeyEnvelopeUnlocked`**：若 **`PutDataBlockBlobUnlocked`** 已成功而后续 **`GetDataBlock`/`UpsertGroupData`** 失败，**尽力 `RevertFailedPutDataBlockUnlocked`**，减少孤儿 **`.zdb`** 区间；轮换时旧块由 **`PutDataBlockBlobUnlocked`** 内先删后写。

```
表: group_data
┌────────────┬────────┬────────┬────────┬────────┐
│ group_id   │ file   │ offset │ length │ sha256 │
│ 群组ID      │ 文件   │ 偏移    │ 长度    │ 校验值  │
└────────────┴────────┴────────┴────────┴────────┘
```

### 2.5 群成员关系

```
表: group_members
┌────────────┬────────────┬──────────┬──────────┐
│ group_id   │ user_id    │  role    │ joined_at│
│ 群组ID      │ 用户ID     │ 角色      │ 加入时间  │
└────────────┴────────────┴──────────┴──────────┘
```

**二进制长度**：**`group_id` / `user_id`** 均为 **`USER_ID_SIZE`（16）BLOB**，与 **`MESSAGE_ID_SIZE` 同为 16** 的 **groupId** 生成一致。**`role`**：**0=member，1=admin，2=owner**；**`SqliteMetadataDb::UpsertGroupMember`** 仅接受 **0/1/2**，**`GetGroupMember`** 读出非三者则失败（防篡改列）。**`GetGroupMemberRowExists`** 仅判断行是否存在（供 **MM1** 在 **role** 异常时仍可判断「是否在群」等语义）。

**`mm2_group_display.name`**：群显示名 **UTF-8**；**`UpsertGroupDisplayName`** 非空且 **≤2048 字节**（与 **`GroupManager::CreateGroup`** 一致）。**创建**时由 **`MM2::CreateGroupSeedForMm1`** 写入首名；**改名**由 **MM1 `GroupNameManager::UpdateGroupName`**（**群主/管理员**）→ **`MM2::UpdateGroupName`**。

### 2.6 即时消息索引（MM2）

```
表: im_messages
┌────────────┬────────────┬──────────────┬────────────┬───────────────────┐
│ message_id │ session_id │ read_at_ms   │ edit_count │ last_edit_time_s  │
│ 16B BLOB   │ 16B BLOB   │ INTEGER NULL │ INTEGER    │ INTEGER (Unix 秒) │
└────────────┴────────────┴──────────────┴────────────┴───────────────────┘
```

与 **`data_blocks`** 配合：`message_id` 与密文块 **`data_id`（chunk_idx=0）** 一致；`session_id` 为 **即时通讯会话通道 ID**（与 **`01-JNI.md`** 中 `imSessionId` 同长约定，当前实现为 **`USER_ID_SIZE` 16 字节**）。**`read_at_ms`**：**NULL**=未读；非 NULL=已读时间（Unix 毫秒）。**`edit_count` / `last_edit_time_s`**：**`MM2::EditMessage` / `GetMessageEditState`**（上层签名校验在 MM1）。**`MM2::MarkMessageRead` / `GetUnreadSessionMessages`** 依赖 **`read_at_ms`**。表内**无**发送时间列；**`MM2::GetSessionMessages`** 与 **`ListImMessageIdsForSession*`**（最近 N / 升序 / 游标）的先后仍依赖 **`rowid`（插入顺序）**。

**v4 附加表（同库）**：**`im_message_reply`**（回复引用，**`message_id`** 为「本条回复」主键）；**`friend_requests`**（MM2 本地好友请求记录）；**`mm2_group_display`**（群显示名，与 **`group_data` 大块索引**独立）；**`mm2_file_transfer`**（逻辑 **`fileId` 字符串**的续传断点、完成 SHA、状态）。详见 **`SqliteMetadataDb`** 与 **`05-ZChatIM-Implementation-Status.md`**。

**v5 附加表（同库）**：**`mm1_user_kv`** — **`user_id`（16B）+ `type`（INTEGER）** 主键；**`data`（BLOB）** 为 MM1/JNI 小型值（如 **`UserDataManager` / Recall 公钥** 类型 **`0x45444A31`**）；**`updated_ms`**（Unix 毫秒）。与 **`user_data`**（**.zdb** 指针 + SHA）**无关**；单条 **`data`** 上限 **16 MiB**（实现校验）。

**v6 附加表（同库）**：**`mm2_group_mute`** — 群禁言（**MM1 `GroupMuteManager`**）；**`PRIMARY KEY (group_id, user_id)`**；**`start_ms`**、**`duration_s`**（**`-1`**=永久；否则结束 **`= start_ms + duration_s * 1000`** ms）、**`muted_by`**（16B）、**`reason`**（BLOB 可空，**≤4096B**）。**`user_version=6`**。旧库 **`CREATE TABLE IF NOT EXISTS`** 于下次 **`InitializeSchema`** 补建。

---

## 三、不存储内容

| 内容 | 存储位置 |
|------|----------|
| 消息明文 | 内存 |
| 消息密文 | .zdb 文件（`StoreMessage`：**AES-GCM** 包；**全平台 OpenSSL 3**；**磁盘字节布局一致**） |
| 文件内容 | .zdb 文件 |
| 密钥 | **内存**（`MM2::Cleanup` 时 **memset 清零**）+ **`indexDir/mm2_message_key.bin`**（**32 字节消息主密钥**；**默认构建**下亦为 **SQLCipher 元数据密钥**的派生根，见 **§4.2**）：**Windows** 新文件为 **ZMK1**（魔数 **`ZMK\1`** + **DPAPI**）；**仍可读**旧 **32 字节明文**并**尽力**改写为 ZMK1。**Linux 等**：新文件为 **ZMK2**（**`ZMK\2`** + **`SHA256(域串‖machine-id‖uid‖indexDir)`** 派生 **32B 封装密钥** + **AES-256-GCM** 保护主密钥；**非** TPM，防「拷走裸 32B」到异机/异用户）。**Apple**：新文件为 **ZMK3**（**`ZMK\3`** + **Keychain** 随机 **32B 封装密钥** + **GCM**）；**`CleanupAllData`** 删密钥文件并 **尽力删除** Keychain 项。**三平台**均**仍可读**旧 **32 字节明文**并**尽力**改写为 ZMK1/ZMK2/ZMK3。 |

---

## 四、安全

### 4.1 访问控制

```
唯一访问者: C++ 可信区（SpringBoot / JNI 不得直连 SQLite 文件）
SpringBoot: 无直接访问权限
```

**说明**：元数据索引的**实现**位于 `ZChatIM` 的 **MM2**（如 **`ZChatIM::mm2::SqliteMetadataDb`**）；与「仅 C++ 可访问」的边界表述一致，并不表示源码目录名必须是 `mm1`。

### 4.2 加密

**默认构建（`ZCHATIM_USE_SQLCIPHER=ON`，见 `ZChatIM/CMakeLists.txt`）**：元数据 **`zchatim_metadata.db`** 使用 **SQLCipher** 页级加密（**非** 仓库内 `thirdparty/sqlite/sqlite3.c` 明文链路）。**内网 / 离线**：工程**不集成 vcpkg**；**`ZCHATIM_USE_SQLCIPHER=ON`** 时**必须**在 **`ZChatIM/thirdparty/sqlcipher/`** 提供 **SQLCipher amalgamation**（**`sqlite3.c` / `sqlite3.h`**），由 CMake **本地编译**（见 **`ZChatIM/thirdparty/sqlcipher/README.md`**）。**OpenSSL** 由 **`OPENSSL_ROOT_DIR`** 或 **`thirdparty/openssl/prebuilt/windows-x64/openssl/`** 等（见 **`ZChatIM/thirdparty/openssl/LAYOUT.md`**）/ 系统 **`libssl-dev`**（Linux）等提供；**MM2 / MM1 / SQLCipher 共用**。**密钥材料**：从 **`indexDir/mm2_message_key.bin`** 的 **32 字节消息主密钥**经固定域串 **`ZChatIM|MM2|SqliteMetadata|SQLCipher|v1`** 做 **SHA-256** 派生 **32 字节 raw key**（与 **`.zdb` 上 AES-GCM** 使用的主密钥**域分离**，避免同一把裸钥多用途）；**`MM2::Initialize`** 在打开元数据库前完成 **`Crypto::Init`** 与主密钥加载/创建。**SQLCipher 参数（写死，跨平台一致）**：**`cipher_page_size=4096`**、**`kdf_iter=256000`**、**`cipher_hmac_algorithm=HMAC_SHA512`**、**`cipher_kdf_algorithm=PBKDF2_HMAC_SHA512`**（见 **`SqliteMetadataDb.cpp`**）。**一次性迁移**：若磁盘上已有旧版 **明文** `SQLite format 3` 库，首次打开会 **`sqlcipher_export`** 到临时文件再替换原路径（旁路文件 **`*.zchatim_sqlcipher_migrate.tmp`**、备份 **`*.pre_sqlcipher.bak`**）；失败时尽力保留/回滚，详见实现注释。

**关闭宏（`ZCHATIM_USE_SQLCIPHER=OFF`）**：回退 **vanilla `sqlite3.c`**，元数据库为**明文页**（仅建议本地调试）；**`.zdb`** 仍为 **AES-GCM** 密文。

**分发与许可**：SQLCipher 为 **Zetetic** 开源许可（**BSD 风格 + 独立声明**）；商业再分发请自行核对 **SQLCipher / OpenSSL** 合规（**全平台** **MM2 / SQLCipher** 均可能链接 **OpenSSL**——见 **`docs/07-Engineering/01-Build-ZChatIM.md`**）。

**已加密不在 SQLite 内的数据**：**IM 消息体**等为 **AES-GCM** 密文，落在 **`.zdb`**（见上文 §三）。

---

## 五、完整性校验

### 5.1 写入校验

```
1. 计算数据 SHA-256
2. 写入 .zdb 文件
3. 记录 sha256 到 SQLite
```

### 5.2 读取校验

```
1. 读取 .zdb 文件
2. 计算 SHA-256
3. 比对 SQLite 中的 sha256
4. 不一致: 标记失效
```

**API 语义（`StorageIntegrityManager::VerifyDataBlockHash`）**：能读到行且未发生参数/DB 错误时返回 **true**，由 **`outMatch`** 表示与库中摘要是否一致；**无行或错误**时返回 **false**（并应查看 `LastError()` / `SqliteMetadataDb::LastError()`）。

---

## 六、`.zdb` 物理布局（v1）

**64 字节文件头 + payload 区**（**opaque**：可为文件分片明文或 **`StoreMessage`** 密文包；固定 **`ZDB_FILE_SIZE`**、小端整数、魔术 **`ZDB\0`**）的字段级说明见 **`04-ZdbBinaryLayout.md`**。实现：`ZdbFile` / `ZdbManager`（MM2）。

---

## 七、C++ 实现落点（`ZChatIM`）

| 组件 | 路径 | 说明 |
|------|------|------|
| `SqliteMetadataDb` | `include/mm2/storage/SqliteMetadataDb.h`、`src/mm2/storage/SqliteMetadataDb.cpp` | 元数据索引库：建表 **zdb_files / … / `mm2_file_transfer` / `mm1_user_kv`**（第2.6节及 **v5**）。**默认 `ZCHATIM_USE_SQLCIPHER`**：链 **`thirdparty/sqlcipher`** amalgamation（**无 vcpkg**）；**`sqlite3_open_v2`** + **`sqlite3_key_v2`** + **§4.2** 固定 PRAGMA；**`Open(path, key32)`**；**明文 SQLite 一次性迁移**（**`sqlcipher_export`**）。**`Open`** 仍含 **`SQLITE_OPEN_FULLMUTEX`**、**`foreign_keys=ON`**、**`busy_timeout(5000)`**。`InitializeSchema`：**`user_version=5`**；**`ALTER TABLE`** 列迁移同前。**关闭宏**时回退 **`thirdparty/sqlite/sqlite3.c`** 明文链路。 |
| `Crypto`（MM2） | `include/mm2/storage/Crypto.h`、`src/mm2/storage/Crypto.cpp` | **全平台 OpenSSL 3**：**`EVP_aes_256_gcm`**、**`PKCS5_PBKDF2_HMAC`**、**`RAND_bytes`**（**Unix** 可再读 **`/dev/urandom`**）。**`nonce(12) ‖ ciphertext ‖ tag(16)`** 与 **PBKDF2 100000 次** 跨平台一致。**`HashSha256`** → **`crypto::Sha256`**。**`GenerateSecureRandom`**：多路径仍失败则返回**空向量**（**`ZdbFile::Create`** 等失败闭环，见 **`05` 第2.1节**）。CMake：**`OpenSSL::Crypto`**；**Windows** 另 **`crypt32`**（**DPAPI** **`mm2_message_key.bin`**）。 |
| `StorageIntegrityManager` | `include/mm2/storage/StorageIntegrityManager.h`、`src/mm2/storage/StorageIntegrityManager.cpp` | 第5节 闭环：`ComputeSha256`（**`crypto::Sha256`**）、`Bind(SqliteMetadataDb*)` 后 **`RecordDataBlockHash` → `UpsertDataBlock`**、**`VerifyDataBlockHash` → `GetDataBlock`** 比对。与 `SqliteMetadataDb` 一致校验 **`dataId` 长度、`chunkIndex <= INT_MAX`、非空 `file_id`**。 |
| `crypto::Sha256` | `include/mm2/crypto/Sha256.h`、`src/mm2/crypto/Sha256.cpp` | **OpenSSL 3** **`EVP_sha256`**（一次性 / 增量 **`Sha256Hasher`**），供完整性链与其它模块复用。 |
| `ZdbFile` / `ZdbManager` | `include/mm2/storage/ZdbFile.h`、`ZdbManager.h`；`src/mm2/storage/ZdbFile.cpp`、`ZdbManager.cpp` | **第六节 / `04-ZdbBinaryLayout.md`**（含 **`totalSize` 严格等于 `ZDB_FILE_SIZE`**、`ZDB_MAX_WRITE_SIZE`、`fileId` basename 规则、`Create` 预填 payload 用 **`Crypto::GenerateSecureRandom`**；**若 CSPRNG 全路径失败**（返回字节数不足）则 **`Create` 失败**并 **`LastError`**；`AllocateSpace` 写 **0** 预留、**`ZDB_MIN_FILES`/`MAX` 未强制** 等实现对照）。`.zdb` v1：`AppendRaw` / `WriteData`；`WriteData` 要求 **`dataId.size() == MESSAGE_ID_SIZE (16)`**，本层不写 SQLite。 |
| `MM2` | `include/mm2/MM2.h`、`src/mm2/MM2.cpp` | **编排层**：`Initialize` 会 **`create_directories`** `dataDir` / `indexDir`（任一步失败则返回 `false`）；`ZdbManager::Initialize(dataDir)` + **`indexDir/zchatim_metadata.db`**（`SqliteMetadataDb` + `StorageIntegrityManager::Bind`）。**`indexDir/mm2_message_key.bin`**（**Windows**：**ZMK1/DPAPI** 新文件，**兼容**旧 **32** 字节明文并**尽力迁移**；**非 Windows**：**32** 字节明文，首次生成）：**`ZCHATIM_USE_SQLCIPHER`** 时在 **`Initialize`** 与 **`Crypto::Init`** 一并加载以打开 SQLCipher；**否则** **`Crypto::Init`** 与该文件在 **`EnsureMessageCryptoReadyUnlocked`** 首次被需要加/解密的入口触发（典型：**`StoreMessage` / `StoreMessages` / `RetrieveMessage` / `RetrieveMessages` / `GetSessionMessages`**（`limit>0`）、**`MessageQueryManager::ListMessages` / `ListMessagesSinceMessageId`**（内部 **`GetSessionMessages`/`RetrieveMessage`**）；**`ListMessagesSinceTimestamp`** 不触达解密，仅返回空并 **`LastError`**）；**`MarkMessageRead` / `GetUnreadSessionMessages`** 仅改查 **`im_messages.read_at_ms`**，**不**调用 **`EnsureMessageCryptoReadyUnlocked`**。**`DeleteMessage` 不调用**（不解密，仅 **`DeleteData`** 清零 + 删 **`data_blocks`/`im_messages`**）。**`StoreFileChunk`** 等仅需 ZDB+SQLite+SIM 即可 **`Initialize`**。**Windows**：**`mm2_message_key.bin`** 用 **`std::filesystem::path::native()` + `_wfopen_s`/`_wfopen`** 读写，与 **`SqliteMetadataDb::Open`**（**宽路径 → UTF-8**；**默认** **`Open(path, key32)`**，**OFF** 时 **`Open(path)`**）的语义一致，避免 **`fstream(path)`** 与元数据库路径不一致。**`StoreFileChunk` / `GetFileChunk`**：`data_blocks` 的 **`data_id`（16 字节）** 由 **`SHA256(fileId 原始字节 ‖ chunkIndex 小端 u32)` 的前 16 字节** 派生；单次分片 **`≤ ZDB_MAX_WRITE_SIZE`**；覆盖同键时先 **`DeleteData`** 再写并 **`RecordDataBlockHash`**。**`StoreMessage` / `StoreMessages` / `RetrieveMessage` / `RetrieveMessages` / `GetSessionMessages`**：`sessionId` 须 **`USER_ID_SIZE`（16）**；**`GetSessionMessages(sessionId, limit)`** 取该会话**最近** **`limit`** 条（**`im_messages.rowid`**），返回 **`(message_id, 明文)`** 且**插入顺序正序**；**任一条解密/校验失败**则清空 **`outMessages`**。**`MessageQueryManager::ListMessages(userId,count)`** 中 **`userId`** 与 **`sessionId` 同义**（16B），返回 **`message_id(16)‖lenBE32‖payload`** 数组；**未 `Initialize` 或已 `Cleanup`**（**`owner_` 空**）时 **`List*`** 返回空且**不**改 **`LastError`**。**`StoreMessages`** 为顺序 **`StoreMessage`**；任一条失败则**回滚**本批已成功写入并**清空** **`outMessageIds`**（**全有或全无**；**`LastError`** 为首条失败原因；**.zdb**/SQLite 仍非单一大事务，极端下某条回滚可能失败——见 **`05` 第8节）；**`RetrieveMessages`** 为全有或全无。载荷经 **AES-GCM** 后写入 **`.zdb`**，blob 布局 **`nonce(12) ‖ ciphertext ‖ tag(16)`**，`data_blocks.data_id = message_id`（`chunk_idx=0`），并写 **`im_messages`**；单条明文上限 **`ZDB_MAX_WRITE_SIZE - 28`**。**`DeleteMessage`**：仅当存在 **`im_messages`** 行时执行；若有 **`data_blocks`** 则先 **`ZdbManager::DeleteData`** 清零对应区间，再在 SQLite 中用 **`DeleteMessageMetadataTransaction`**（**`BEGIN IMMEDIATE`**）**原子**删除 **`data_blocks`（chunk 0）与 `im_messages`**（避免仅删一行造成的半索引；**`.zdb` 与 SQLite 仍非单事务**）。**`Cleanup` / `CleanupUnlocked`**：调用 **`Crypto::Cleanup`** 并对 **`m_messageStorageKey` 做 `memset` 清零**。**已知限制**：v1 **仍非** `.zdb` 与 SQLite **单事务**。**`PutDataBlockBlob` / `StoreFileChunk`**：`WriteData` 成功后若 **`GetFileStatus` / `UpsertZdbFile` / `ComputeSha256` / `RecordDataBlockHash`** 任一步失败，会尽力 **`RevertFailedPutDataBlockUnlocked`**（清零刚追加区间、若存在 **`data_blocks`** 行则删除、刷新 **`zdb_files.used_size`**）。**覆盖写**若已在 **`RecordDataBlockHash` 前 **`DeleteData` 清零旧区间**，后续失败时补偿会删索引行并 scrub 新尾，**旧数据不可恢复**（须重传）。**`InsertImMessage` 在已成功 `PutDataBlockBlob` 之后失败**时同样尽力补偿；补偿失败时 **`LastError`** 会带 **`compensation failed`**。**`DeleteMessage`**：**.zdb 清零** 与 **SQLite** 仍分步；**`data_blocks`/`im_messages`** 在库内由 **`DeleteMessageMetadataTransaction`** 原子提交（详见 **`05-ZChatIM-Implementation-Status.md` 第8节**）。**`DeleteData` 不收缩** `.zdb` 头内 **`usedSize`**（仅覆盖字节为 0；`zdb_files.used_size` 仍可能反映逻辑尾）。 |
