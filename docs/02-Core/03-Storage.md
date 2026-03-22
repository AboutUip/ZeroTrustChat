# 存储机制技术规范

> **持久化取向**：**凡落盘均扩大泄露面**；持久化须**严格收紧**（白名单、最短必要、生命周期、可销毁）。**禁止**未经安全评审新增持久化路径。详见 **`docs/README.md` 第2节**、仓库根 **`README.md` 设计原则**。

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

### 2.6 即时消息（MM2，仅 RAM）

> **当前实现**：**`StoreMessage` / 列表 / 已读 / 编辑 / 回复关系** 均在 **`MM2` 进程内 RAM**（见 **`05-ZChatIM-Implementation-Status.md`**）。**元数据库****不**含 **`im_messages` / `im_message_reply`**；**`MM2::Initialize`** 在 **`SqliteMetadataDb::InitializeSchema`**（**`user_version=11`**）之后仅调用 **`ImRamClearUnlocked()`** 清空**本进程** IM 内存，**不**做旧 IM SQLite/`.zdb` 迁移或扫盘清理。

下列为 **逻辑字段**（与历史文档中「`im_messages` 列」**语义对齐**，**非**当前 SQLite 表）：

```
逻辑字段（ImRam 行，示意）
┌────────────┬────────────┬─────────────────┬────────────────┬──────────────┬────────────┬───────────────────┐
│ message_id │ session_id │ sender_user_id  │ stored_at_ms   │ read_at_ms   │ edit_count │ last_edit_time_s  │
│ 16B        │ 16B        │ 16B (可 NULL)   │ int64 ms       │ int64 ms     │ int        │ int64 Unix 秒     │
└────────────┴────────────┴─────────────────┴────────────────┴──────────────┴────────────┴───────────────────┘
```

**密文**：**`ImRamMessageRow.blob`** 为 **AES-GCM** 包 **`nonce(12) ‖ ciphertext ‖ tag(16)`**（字节布局与历史 **`.zdb` chunk0** 文献一致，**默认不落盘**）。**`GetSessionMessages` / `ListMessages*`** 按内存索引与 **`stored_at_ms`** 过滤。**`NULL sender_user_id`**：**MM1** 编辑/撤回拒绝；**新消息** RAM 路径**须**带 16B 发送者。

**同库持久化（与 IM 分离）**：**`friend_requests`**、**`mm2_group_display`**、**`mm2_file_transfer`** 等见 **`SqliteMetadataDb`**。**`mm1_device_sessions` / `mm1_im_session_activity` / `mm1_cert_pin_config` / `mm1_cert_pin_client` / `mm1_user_status` / `mm1_mention_atall_window`** — **MM1** 多设备登记、IM 通道活跃、证书 Pin、**最后已知在线态**、**@ALL** 限速窗等。**`data_blocks`** 用于 **文件分片** 与完整性索引（**`StoreFileChunk`**），**不**作为当前默认 IM 热路径。**`mm1_user_kv`** — **`user_id`（16B）+ `type`（INTEGER）** 主键；**`data`（BLOB）** 为 MM1/JNI 小型值；单条 **`data`** 上限 **16 MiB**。**`mm2_group_mute`** — 群禁言（**MM1 `GroupMuteManager`**）；**`MM2::DeleteGroupMemberForMm1`** 在删 **`group_members`** 行前先删对应 **`mm2_group_mute`** 行。

**`user_version=11`**：**`InitializeSchema`** 创建当前元表集并 **`PRAGMA user_version=11`**（含 **`mm1_device_sessions` / `mm1_im_session_activity` / `mm1_cert_pin_*` / `mm1_user_status` / `mm1_mention_atall_window`**）；**不含** IM 表（本分支**不**维护历史 **`im_messages`/`im_message_reply` 迁移**）。**自 v10 库**：**`CREATE TABLE IF NOT EXISTS`** 补齐新表并将 **`user_version`** 置 **11**。

---

## 三、不存储内容

| 内容 | 存储位置 |
|------|----------|
| 消息明文 | 内存 |
| 消息密文（IM） | **进程内 RAM**（**AES-GCM** 包，字节布局与历史 **`.zdb` chunk0** 一致；**不落盘**）。**文件分片等**仍见 **`.zdb`**。 |
| 文件内容 | .zdb 文件 |
| 密钥 | **内存**（`MM2::Cleanup` 时 **memset 清零**）+ **`indexDir/mm2_message_key.bin`**（**32 字节消息主密钥**；**默认构建**下亦为 **SQLCipher 元数据密钥**的派生根，见 **第4.2节**）：**Windows** 新文件为 **ZMK1**（魔数 **`ZMK\1`** + **DPAPI**）；**仍可读**旧 **32 字节明文**并**尽力**改写为 ZMK1。**Linux 等**：新文件为 **ZMK2**（**`ZMK\2`** + **`SHA256(域串‖machine-id‖uid‖indexDir)`** 派生 **32B 封装密钥** + **AES-256-GCM** 保护主密钥；**非** TPM，防「拷走裸 32B」到异机/异用户）。**Apple**：新文件为 **ZMK3**（**`ZMK\3`** + **Keychain** 随机 **32B 封装密钥** + **GCM**）；**`CleanupAllData`** 删密钥文件并 **尽力删除** Keychain 项。**三平台**均**仍可读**旧 **32 字节明文**并**尽力**改写为 ZMK1/ZMK2/ZMK3。 |

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

**已加密且不在 SQLite 内的数据**：**IM 消息体**为 **AES-GCM** 密文，**默认仅驻留进程 RAM**；**文件分片等内容**为 **AES-GCM** 密文落在 **`.zdb`**（见上文 **第三节**）。

### 4.3 运维与密钥恢复（产品口径）

以下供**客服 / 发布说明 / 集成方**与实现**统一说法**（默认构建 **`ZCHATIM_USE_SQLCIPHER=ON`**、**ZMK1/2/3** 见 **第三节** 与 **`MM2.cpp`**）：

1. **丢失或删除 `indexDir` 下关键文件**（至少含 **`mm2_message_key.bin`**、**`zchatim_metadata.db`**）：若无**另行实现**的备份/导出，本地 **SQLCipher** 与 **`.zdb` 密文**通常**无法恢复**；**IM 与元数据**须按**服务端重新同步**等产品策略处理。
2. **换机 / 重装**：**不能**假设「拷贝整个数据目录到另一台机器即可继续解密」。**ZMK1** 绑定 **Windows** 当前用户 **DPAPI**；**ZMK2** 绑定 **机器标识 / 有效用户 / `indexDir` 路径**；**ZMK3** 绑定 **Apple Keychain**。新设备上的历史消息是否可见由**服务端下发与客户端同步逻辑**决定。
3. **多用户同机**：多个 OS 登录用户或应用内账号若**共用同一 `dataDir` / `indexDir`**，即共用同一本地密钥语境；**推荐**按账号划分**独立目录**（由上层传入 **`MM2::Initialize(dataDir, indexDir)`**）。
4. **销毁本地数据**：**`MM2::CleanupAllData`** 在 **`CleanupUnlocked`**（清零进程内 **`m_messageStorageKey`**、关库）之后，会**删除** **`dataDir` 下 `*.zdb`**、**元数据库文件**、**`mm2_message_key.bin`**（路径已知时）；**Apple** 上另调用 **`Mm2DarwinDeleteMessageWrapKey`** **尽力删除** **ZMK3** 的 Keychain 项。**`MM2::Cleanup`**（常规退出）**不删除**盘上 **`mm2_message_key.bin`**，下次 **`Initialize`** 仍可读同一密钥文件。**`MM1::EmergencyTrustedZoneWipe`**：若 **MM2 已初始化**，会调用 **`MM2::CleanupAllData()`**（见 **`MM1.cpp`**），故**会删**上述盘上文件与 **`mm2_message_key.bin`**；并清 **MM1** 进程内态（见 **`JniSecurityPolicy.h` 第8节**）。**纯 `MM2::Cleanup`** 与 **紧急全量销毁** 的磁盘效果**不同**，集成方须区分。

### 4.4 持久化 IM 的泄露面、销毁责任与产品取舍（重要）

**与「IM 仅 RAM」并存的风险口径**：**IM 正文**默认**不**落在 **`.zdb`/元库**；若产品**未来**改为本地持久化 IM 或长期保留 **RAM 导出**，则**同一 `dataDir`/`indexDir` 上积累的聊天密文规模**会上升。当前默认下，攻击者取得 **`mm2_message_key.bin` + 目录** 主要威胁 **文件分片、群/好友等已落盘密文与元数据**；**进程已结束的 IM** 若无另行导出则**不在盘上**。全磁盘加密与 **`CleanupAllData`** 仍是基础缓解（见 **4.3**）。

**「所有用户全部泄露」的表述须分清边界**：

- **单台客户端、单一数据目录**：泄露面主要是**曾使用该目录的账号**在本机上的本地副本，**不是**自动等于「服务端上全体注册用户」；**服务端全库**安全属于**后端与 TLS/访问控制**，不在本文档范围。
- **多账号共用同一 `indexDir`**：等价于**共用根密钥语境**，一处失守影响该目录下**所有曾写入的账号数据**；须由产品层**分目录**或**分密钥**（见 **4.3 第3条**）。

**数据销毁在何处（实现已提供的杠杆）**：

| 动作 | 盘上效果（摘要） |
|------|------------------|
| **`MM2::CleanupAllData`** | 删 **`*.zdb`**、**元数据库**、**`mm2_message_key.bin`**（及 Apple **Keychain** 尽力删 **ZMK3** 项） |
| **`MM1::EmergencyTrustedZoneWipe`** / **`JniInterface::EmergencyWipe`**（JNI） | 上述 **MM2 全清**（若已初始化）+ **MM1 进程内表**（会话、多设备登记等）+ **`JniBridge::m_initialized=false`**（见 **`JniSecurityPolicy.h` 第8条**） |
| **`MM2::Cleanup`** | **不删**密钥文件与库；**仅**关连接、清零内存中的消息主密钥材料；**登出即不留盘**须由**产品**显式调用 **`CleanupAllData`** 或删目录，**非**默认自动行为。 |

**缓解「整库被拖走」方向（工程/产品，非本节能穷举）**：**OS 全磁盘加密**、**屏幕锁定**、**缩短本地保留 / 定期清库**、**账号级独立 `indexDir`**、**M2 增强**（口令裹密钥，见 **`05` 第7.3节**）、以及若必须坚持 **「无本地历史」** 则须 **M3 级**换存储策略（见 **`docs/07-Engineering/02-Cpp-Completion-Roadmap.md` 第5节** 与 **`01-MM1.md` 一点五**）。

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

**64 字节文件头 + payload 区**（**opaque**：**文件分片**、**`group_data` / `user_data` 大块**等；**默认 IM 密文不落此文件**——**`StoreMessage`** 走 **RAM**，见 **第2.6节**；固定 **`ZDB_FILE_SIZE`**、小端整数、魔术 **`ZDB\0`**）的字段级说明见 **`04-ZdbBinaryLayout.md`**。实现：`ZdbFile` / `ZdbManager`（MM2）。

---

## 七、C++ 实现落点（`ZChatIM`）

| 组件 | 路径 | 说明 |
|------|------|------|
| `SqliteMetadataDb` | `include/mm2/storage/SqliteMetadataDb.h`、`src/mm2/storage/SqliteMetadataDb.cpp` | 元数据索引库：建表 **zdb_files / … / `mm2_file_transfer` / `mm1_user_kv` / `mm2_group_mute` / `mm1_device_sessions` / `mm1_im_session_activity` / `mm1_cert_pin_*` / `mm1_user_status` / `mm1_mention_atall_window`**（第2.6节）；**不含** **`im_messages` / `im_message_reply`**（**`user_version=11`**）。**默认 `ZCHATIM_USE_SQLCIPHER`**：链 **`thirdparty/sqlcipher`** amalgamation（**无 vcpkg**）；**`sqlite3_open_v2`** + **`sqlite3_key_v2`** + **第4.2节** 固定 PRAGMA；**`Open(path, key32)`**；**明文 SQLite 一次性迁移**（**`sqlcipher_export`**）。**`Open`** 仍含 **`SQLITE_OPEN_FULLMUTEX`**、**`foreign_keys=ON`**、**`busy_timeout(5000)`**。`InitializeSchema`：**`CREATE` 当前 schema + `PRAGMA user_version=11`**（**无** IM 表、**无** IM 相关迁移分支）。**关闭宏**时回退 **`thirdparty/sqlite/sqlite3.c`** 明文链路。 |
| `Crypto`（MM2） | `include/mm2/storage/Crypto.h`、`src/mm2/storage/Crypto.cpp` | **全平台 OpenSSL 3**：**`EVP_aes_256_gcm`**、**`PKCS5_PBKDF2_HMAC`**、**`RAND_bytes`**（**Unix** 可再读 **`/dev/urandom`**）。**`nonce(12) ‖ ciphertext ‖ tag(16)`** 与 **PBKDF2 100000 次** 跨平台一致。**`HashSha256`** → **`crypto::Sha256`**。**`GenerateSecureRandom`**：多路径仍失败则返回**空向量**（**`ZdbFile::Create`** 等失败闭环，见 **`05` 第2.1节**）。CMake：**`OpenSSL::Crypto`**；**Windows** 另 **`crypt32`**（**DPAPI** **`mm2_message_key.bin`**）。 |
| `StorageIntegrityManager` | `include/mm2/storage/StorageIntegrityManager.h`、`src/mm2/storage/StorageIntegrityManager.cpp` | 第5节 闭环：`ComputeSha256`（**`crypto::Sha256`**）、`Bind(SqliteMetadataDb*)` 后 **`RecordDataBlockHash` → `UpsertDataBlock`**、**`VerifyDataBlockHash` → `GetDataBlock`** 比对。与 `SqliteMetadataDb` 一致校验 **`dataId` 长度、`chunkIndex <= INT_MAX`、非空 `file_id`**。 |
| `crypto::Sha256` | `include/mm2/crypto/Sha256.h`、`src/mm2/crypto/Sha256.cpp` | **OpenSSL 3** **`EVP_sha256`**（一次性 / 增量 **`Sha256Hasher`**），供完整性链与其它模块复用。 |
| `ZdbFile` / `ZdbManager` | `include/mm2/storage/ZdbFile.h`、`ZdbManager.h`；`src/mm2/storage/ZdbFile.cpp`、`ZdbManager.cpp` | **第六节 / `04-ZdbBinaryLayout.md`**（含 **`totalSize` 严格等于 `ZDB_FILE_SIZE`**、`ZDB_MAX_WRITE_SIZE`、`fileId` basename 规则、`Create` 预填 payload 用 **`Crypto::GenerateSecureRandom`**；**若 CSPRNG 全路径失败**（返回字节数不足）则 **`Create` 失败**并 **`LastError`**；`AllocateSpace` 写 **0** 预留、**`ZDB_MIN_FILES`/`MAX` 未强制** 等实现对照）。`.zdb` v1：`AppendRaw` / `WriteData`；`WriteData` 要求 **`dataId.size() == MESSAGE_ID_SIZE (16)`**，本层不写 SQLite。 |
| `MM2` | `include/mm2/MM2.h`、`src/mm2/MM2.cpp` | **编排层**：`Initialize`：**`create_directories`** → **`ZdbManager::Initialize(dataDir)`** → **`indexDir/zchatim_metadata.db`**（**`SqliteMetadataDb::Open` + `InitializeSchema`（v11，无 IM 表）**）→ **`StorageIntegrityManager::Bind`** → **`ImRamClearUnlocked()`** → **`m_initialized`** + **`MessageQueryManager::SetOwner(this)`**。**`mm2_message_key.bin`**（**ZMK1/2/3**）与 **第三节**、**`MM2.cpp` / `MM2_message_key_darwin.cpp`** 一致。**IM（RAM）**：**`StoreMessage` / `StoreMessages`** 加密写入内存索引；**`RetrieveMessage` / `RetrieveMessages` / `GetSessionMessages`**、**`MessageQueryManager::ListMessages` / `ListMessagesSinceMessageId` / `ListMessagesSinceTimestamp`**（**`ImRamListIds*Unlocked` + `RetrieveMessage`**，须 **`EnsureMessageCryptoReadyUnlocked`**）；**`MarkMessageRead` / `GetUnreadSessionMessages`** 仅 RAM 行；**`DeleteMessage` / `EditMessage`** → **`ImRamEraseUnlocked` / 内存内替换 blob 与编辑计数**，**不**动 **`StoreFileChunk`** 的 **`data_blocks`**。**`List*`**： **`owner_==nullptr`** 时返回空且**不**改 **`LastError`**。**`StoreMessages`**：**全有或全无**（失败则 **`ImRamEraseUnlocked`** 回滚本批）。**文件分片**：**`StoreFileChunk` / `GetFileChunk`** — **`data_id` = SHA256(fileId‖chunk LE32) 前 16B**，**`≤ ZDB_MAX_WRITE_SIZE`**，**`RecordDataBlockHash`** 链失败时 **`RevertFailedPutDataBlockUnlocked`**（见 **`05` 第8节**）。**`Cleanup` / `CleanupUnlocked`**：**`Crypto::Cleanup`** + 消息主密钥 **`memset`**。**Windows** 宽路径与 **`SqliteMetadataDb::Open`** UTF-8 约定一致。v1 **无** 跨 **`.zdb` 与 SQLite** 的**单事务**；**`DeleteData` 不收缩** `.zdb` **`usedSize`**。 |
