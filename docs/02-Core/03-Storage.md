# 存储机制技术规范

## 一、职责

**仅作为索引和元数据存储，不存储实际业务数据。**

---

## 二、存储内容

### 2.1 文件索引

```
表: zdb_files
┌────────────┬────────────┬────────────┐
│ file_id    │ total_size │ used_size  │
│ 随机文件名   │  512000    │ 已用空间    │
└────────────┴────────────┴────────────┘
```

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

---

## 三、不存储内容

| 内容 | 存储位置 |
|------|----------|
| 消息明文 | 内存 |
| 消息密文 | .zdb 文件 |
| 文件内容 | .zdb 文件 |
| 密钥 | 内存 / .zdb (加密) |

---

## 四、安全

### 4.1 访问控制

```
唯一访问者: C++ 可信区（SpringBoot / JNI 不得直连 SQLite 文件）
SpringBoot: 无直接访问权限
```

**说明**：元数据索引的**实现**位于 `ZChatIM` 的 **MM2**（如 `mm2::SqliteMetadataDb`）；与「仅 C++ 可访问」的边界表述一致，并不表示源码目录名必须是 `mm1`。

### 4.2 加密

```
数据库加密: SQLCipher
密钥管理: 由 MM1 生成和管理
```

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

## 七、C++ 实现落点（`ZChatIM`）

| 组件 | 路径 | 说明 |
|------|------|------|
| `SqliteMetadataDb` | `include/mm2/storage/SqliteMetadataDb.h`、`src/mm2/storage/SqliteMetadataDb.cpp` | 元数据索引库：建表 **zdb_files / data_blocks / user_data / group_data / group_members**（与 §二 对齐）；**每次 `Open` 后**对连接执行 `PRAGMA foreign_keys=ON`；`InitializeSchema` 内再次保证并设置 **`user_version=1`**（与 `kSchemaUserVersion` 一致）。**`InsertDataBlock` / `UpsertDataBlock` / `GetDataBlock` / `DataBlockExists`** 等前置校验见 **§2.2、§2.6**。当前为 **vanilla SQLite**；§4.2 所述 **SQLCipher** 待密钥接入后再换。SQLite 源码：`thirdparty/sqlite/` amalgamation。 |
| `StorageIntegrityManager` | `include/mm2/storage/StorageIntegrityManager.h`、`src/mm2/storage/StorageIntegrityManager.cpp` | §5 闭环：`ComputeSha256`（Windows **BCrypt** / 其他平台可移植实现）、`Bind(SqliteMetadataDb*)` 后 **`RecordDataBlockHash` → `UpsertDataBlock`**、**`VerifyDataBlockHash` → `GetDataBlock`** 比对。与 `SqliteMetadataDb` 一致校验 **`dataId` 长度、`chunkIndex <= INT_MAX`、非空 `file_id`**。 |
| `crypto::Sha256` | `include/mm2/crypto/Sha256.h`、`src/mm2/crypto/Sha256.cpp` | 纯 SHA-256 工具，供完整性链与其它模块复用。 |

自检：`ZChatIM --test` 中 **SqliteMetadataDb** 用例（临时 DB：`zdb_files` / `data_blocks` 外键、`user_data` Upsert/Get 与 FK、`group_data` / `group_members` / `DeleteGroupMember`）；**StorageIntegrityManager** 用例（空串 SHA-256 向量、写入临时 `.zdb` 后 Record/Verify）。
