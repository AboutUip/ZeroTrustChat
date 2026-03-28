# 存储机制技术规范

> **持久化**：落盘扩大泄露面；须白名单与生命周期审定。新增持久化须评审。权威链见 [`AUTHORITY.md`](../AUTHORITY.md)。

> **与「IM 是否落盘」冲突**：以 [`AUTHORITY.md`](../AUTHORITY.md)、[`Implementation-Status.md`](../../ZChatIM/docs/Implementation-Status.md) 为准。

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

**`ZGK1` 信封**：`group_id`（16B）作 `data_blocks` chunk0 的 `data_id`；`.zdb` 载荷 `ZGK1‖epoch_u64BE‖32B 随机`（44B）。`group_data` 指向块位置。建群写首包；`UpsertGroupKeyEnvelopeForMm1` 轮换。写失败路径尽力 `RevertFailedPutDataBlockUnlocked`（见 `MM2` 实现）。

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

**`mm2_group_display`**：群名 UTF-8 ≤2048B；创建时 `CreateGroupSeedForMm1` 写首名；改名经 `GroupNameManager`→`MM2::UpdateGroupName`。

### 2.6 即时消息（MM2，仅 RAM）

> **IM**：`StoreMessage`/列表/已读/编辑/回复关系均在 **MM2 RAM（ImRam）**；元库**无** `im_messages`/`im_message_reply`。`Initialize` 在 schema v11 后 `ImRamClearUnlocked()`，不迁移历史 IM 表。详 [`AUTHORITY.md`](../AUTHORITY.md)、[`Implementation-Status.md`](../../ZChatIM/docs/Implementation-Status.md)。

**逻辑字段**（与历史 `im_messages` 列语义对齐，**非** SQLite 表）：

```
逻辑字段（ImRam 行，示意）
┌────────────┬────────────┬─────────────────┬────────────────┬──────────────┬────────────┬───────────────────┐
│ message_id │ session_id │ sender_user_id  │ stored_at_ms   │ read_at_ms   │ edit_count │ last_edit_time_s  │
│ 16B        │ 16B        │ 16B (可 NULL)   │ int64 ms       │ int64 ms     │ int        │ int64 Unix 秒     │
└────────────┴────────────┴─────────────────┴────────────────┴──────────────┴────────────┴───────────────────┘
```

**密文**：`ImRamMessageRow.blob` 为 AES-GCM `nonce(12)‖ciphertext‖tag(16)`（与 `.zdb` chunk0 包同形，**默认不落盘**）。`List*` 按 `stored_at_ms`。`sender_user_id` 空则 MM1 编辑/撤回拒绝。

**同库表（非 IM 正文）**：`friend_requests`、`mm2_group_display`、`mm2_file_transfer`、`data_blocks`（文件分片）、`mm1_user_kv`、`mm2_group_mute`、`mm1_device_sessions`、`mm1_im_session_activity`、`mm1_cert_pin_*`、`mm1_user_status`、`mm1_mention_atall_window` 等——见 `SqliteMetadataDb` / 第七节表。`mm1_user_kv`：`user_id`+`type` PK，`data`≤16MiB；**`type` 为任意 int32**（如本地口令 **LPH1/LRC1**、头像 **`MM1_USER_KV_TYPE_AVATAR_V1`（AVT1）**、展示昵称 **`MM1_USER_KV_TYPE_DISPLAY_NAME_V1`（NMN1）** 等），经 **`UserDataManager` → `MM2::StoreMm1UserDataBlob`** 与 JNI **`storeUserData`** 同源持久化。退群/踢人前删 `mm2_group_mute` 对应行。

**`user_version=11`**：当前 schema；无 IM 消息表；自 v10 用 `CREATE IF NOT EXISTS` 补齐后置 11。

---

## 三、不存储内容

| 内容 | 存储位置 |
|------|----------|
| 消息明文 | 内存 |
| 消息密文（IM） | 进程内 RAM（AES-GCM 包，同形于 `.zdb` chunk0；**不落盘**）。文件载荷在 `.zdb`。 |
| 文件内容 | `.zdb` |
| 密钥 | 运行期内存（`Cleanup` 清零）+ `indexDir/mm2_message_key.bin`（32B 消息主密钥；亦为 SQLCipher 派生根，**第4.2节**）。封装：**ZMK1**（Win DPAPI）、**ZMK2**（域串‖machine-id‖uid‖indexDir 派生封装钥）、**ZMK3**（Apple Keychain）；均可读旧明文 32B 并尽力升级。详 `MM2.cpp` / `Build.md`。 |

---

## 四、安全

### 4.1 访问控制

```
唯一访问者: C++ 可信区（SpringBoot / JNI 不得直连 SQLite 文件）
SpringBoot: 无直接访问权限
```

**说明**：元数据索引的**实现**位于 `ZChatIM` 的 **MM2**（如 **`ZChatIM::mm2::SqliteMetadataDb`**）；与「仅 C++ 可访问」的边界表述一致，并不表示源码目录名必须是 `mm1`。

### 4.2 加密

**默认 `ZCHATIM_USE_SQLCIPHER=ON`**：`zchatim_metadata.db` 用 SQLCipher；树内 `thirdparty/sqlcipher/` amalgamation；OpenSSL 见 [`Build.md`](../../ZChatIM/docs/Build.md)、`thirdparty/openssl/LAYOUT.md`。元库 **raw key**：自 `mm2_message_key.bin` 32B 经域串 `ZChatIM|MM2|SqliteMetadata|SQLCipher|v1` SHA-256 派生，与 `.zdb` AES-GCM 主密钥**分离**。PRAGMA（page 4096、kdf_iter、HMAC/KDF 算法）写死在 **`SqliteMetadataDb.cpp`**。明文旧库首次打开 **`sqlcipher_export`** 迁移（临时/备份文件名见实现注释）。

**`OFF`**：vanilla sqlite，元库明文；`.zdb` 仍为 AES-GCM。

**许可**：SQLCipher（Zetetic）与 OpenSSL 再分发责任由集成方自负。

**IM 体**：默认仅 RAM；文件块在 `.zdb`（**第三节**）。

### 4.3 运维与密钥恢复（产品口径）

**口径**（ZMK 见第三节、`MM2.cpp`）：

1. 丢失 `mm2_message_key.bin` 与元库且无备份：本地密文通常**不可解**；靠服务端重同步等。  
2. 换机：ZMK1/2/3 绑定本机/用户/Keychain；勿假定拷目录即可解密。  
3. 多账号：勿共用同一 `dataDir`/`indexDir`，除非有意共密钥语境。  
4. **`CleanupAllData`**：删 `*.zdb`、元库、`mm2_message_key.bin`（Apple 另尽力删 Keychain）。**`Cleanup`** 不删密钥文件。**`EmergencyTrustedZoneWipe`**（MM2 已初始化时）→ `CleanupAllData` + MM1 态；与 `JniSecurityPolicy.h` 第8节、`MM1.cpp` 一致。

### 4.4 泄露面与销毁（摘要）

默认 **IM 正文不**进 `.zdb`/元库；持 `mm2_message_key.bin`+目录主要威胁**已落盘**的文件块、群/友元数据等。单设备泄露≠服务端全库泄露；共用 `indexDir` 则共密钥语境。

| 动作 | 盘上效果 |
|------|----------|
| `CleanupAllData` | 删 `*.zdb`、元库、`mm2_message_key.bin`（及 Apple Keychain 尽力项） |
| `EmergencyTrustedZoneWipe` / JNI `EmergencyWipe` / `SystemControl::EmergencyWipe` | MM2 全清（若已初始化）+ MM1 表；经 `Notify*` 路径置 `JniBridge::m_initialized=false`；仅直调 `MM1::EmergencyTrustedZoneWipe` 不改桥接标志（`JniSecurityPolicy.h` 第8节） |
| `Cleanup` | 不删盘上密钥与库；关库、清零内存密钥 |

缓解：全盘加密、分账号目录、ZMKP/口令、定期清库；「无本地历史」属 M3 与 [`Scope.md`](../../ZChatIM/docs/Scope.md)、`01-MM1` 一点五。

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
| `SqliteMetadataDb` | `mm2/storage/SqliteMetadataDb.{h,cpp}` | 元库表集见第2.6节；`user_version=11`；无 IM 消息表。SQLCipher 默认；`Open`+PRAGMA+明文迁移见第4.2节与源码。 |
| `Crypto` | `mm2/storage/Crypto.{h,cpp}` | OpenSSL 3：AES-GCM、PBKDF2、RNG；`GenerateSecureRandom` 失败返回空（影响 `ZdbFile::Create` 等）。 |
| `StorageIntegrityManager` | `mm2/storage/StorageIntegrityManager.{h,cpp}` | 块 SHA-256 与 `data_blocks` 记录/比对（第5节）。 |
| `crypto::Sha256` | `mm2/crypto/Sha256.{h,cpp}` | `EVP_sha256` / `Sha256Hasher`。 |
| `ZdbFile` / `ZdbManager` | `mm2/storage/` | v1 布局与约束见 [`04-ZdbBinaryLayout.md`](04-ZdbBinaryLayout.md)、`Types.h`；`WriteData` 的 `dataId` 16B。 |
| `MM2` | `mm2/MM2.{h,cpp}` | 编排：`Initialize`（目录→Zdb→元库 v11→Integrity→ImRam→`MessageQueryManager`）；IM 全 RAM（存取、List、已读、编辑、删）；`StoreMessages` 批回滚；文件分片与 `RevertFailedPutDataBlockUnlocked` 见 [`Implementation-Status`](../../ZChatIM/docs/Implementation-Status.md) 第8节；无跨 `.zdb`+SQLite 单事务；`Cleanup`/`CleanupAllData` 见第4.3节。 |
