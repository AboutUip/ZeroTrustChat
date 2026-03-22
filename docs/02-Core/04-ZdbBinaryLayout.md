# `.zdb` 容器二进制布局（v1）

本文档描述 **MM2** 中 `.zdb` 文件的 **v1** 物理格式，与 `ZChatIM/include/Types.h` 中的 `ZdbHeader`（`ZChatIM_ZdbHeaderLayout`）、`MAGIC_ZDB`、`ZDB_FILE_SIZE` 及实现 `ZdbFile` / `ZdbManager` 对齐。

## 1. 设计要点

| 项 | v1 行为 |
|----|---------|
| 文件总长 | 固定 **`ZDB_FILE_SIZE`**（当前 **5 MiB**，与 `Types.h` 一致） |
| 头部 | **64 字节** `ZdbHeader`，其后为 **payload** 区 |
| 写入路径 | **仅尾部追加**（`AppendRaw` / `ZdbManager::WriteData`）；**容器层无加密**、无空闲块回收表（payload 当前主要为**文件分片**与 **`group_data`/`user_data` 大块**等；**默认 IM** 经 **`StoreMessage`** **不落 `.zdb`**，见 **`03-Storage.md` 第2.6节**；**字节布局**上 IM 密文可与历史 **chunk0 AES-GCM 包**同形 **`nonce‖ciphertext‖tag`**） |
| 头部校验域 | `checksum[32]` 预留，v1 填 **0** |
| 尾部 | 创建时头之后至文件尾用 **`Crypto::GenerateSecureRandom`** 预填**高熵字节**（**OpenSSL `RAND_bytes`**；**Unix** 可再读 **`/dev/urandom`**）；**任一分块取随机失败则 `Create` 整体失败**（不写半初始化文件）；**`AllocateSpace` / `DeleteData` 等预留区仍写 0x00**（见 `ZdbFile`） |
| 打开校验 | **`ZdbFile::Open` / `Create`**：`version==1`，**磁盘文件字节数**须等于 **`header.totalSize`**（即 **`ZDB_FILE_SIZE`**），**`usedSize`** 须在 **`[64, totalSize]`** |

## 2. `ZdbHeader`（64 字节，`#pragma pack(push, 1)`）

所有多字节整数均为 **小端（LE）**。下列 **偏移** 自文件开头。

| 偏移 | 长度 | 类型 / 含义 |
|------|------|-------------|
| 0x00 | 4 | **`magic`**：`MAGIC_ZDB` = `'Z' 'D' 'B' '\0'`（`0x5A 44 42 00`） |
| 0x04 | 1 | **`version`**：v1 = **1** |
| 0x05 | 7 | **`reserved`**：v1 填 **0** |
| 0x0C | 4 | **`padding`**：v1 填 **0** |
| 0x10 | 8 | **`totalSize`**：`uint64_t`，**必须等于**文件总长度；实现上 **`ReadHeader` 强制 `totalSize == ZDB_FILE_SIZE`**（与 `Types.h` 一致） |
| 0x18 | 8 | **`usedSize`**：`uint64_t`，**逻辑已用尾边界**（含 64 字节头）；下一条 **`AppendRaw`** 从 **`usedSize`** 起写；初始创建后为 **64** |
| 0x20 | 32 | **`checksum`**：v1 全 **0** |

`static_assert(sizeof(ZdbHeader) == 64)`（见 `ZdbFile.cpp`）。

## 3. Payload 区

- **有效范围**：`[64, totalSize)`。
- **第一条追加记录**起点偏移为 **64**（此时创建后 `usedSize == 64`，追加后 `usedSize == 64 + length`）。
- 读取/校验时，**元数据**（`SqliteMetadataDb` / `StorageIntegrityManager`）中的 **`offset` / `length`** 指 payload 区内字节范围（可与头 64 分开存，也可从 64 起算，须与写入约定一致；**首条 payload 常见 `offset == 64`**）。
- **opaque 内容**：`data_blocks.length` 字节可为任意八位组。**当前热路径**下多为**文件分片**等；若某块为 **AES-GCM 应用内联密文**，则为 **`nonce(12) ‖ ciphertext ‖ tag(16)`**（与 **`03-Storage.md` / `ImRamMessageRow.blob`** 同形），**`ComputeSha256` 仍针对磁盘原始字节**。

## 4. 与 SQLite 索引的关系

- `zdb_files`：`file_id` 与磁盘文件名一致；`total_size` / `used_size` 应与头内 `totalSize` / `usedSize` 语义一致（由上层在写入后维护）。
- `data_blocks`：`data_id`（16 字节）、`file_id`、`offset`、`length`、`sha256` 描述一块 payload；**`ComputeSha256` / 校验始终针对磁盘上该区间原始字节**（明文分片或应用层写入的 **AES-GCM 包**，不在此层区分）。**默认 IM 不登记 `data_blocks`。**
- **文件分片（`MM2::StoreFileChunk`）**：`data_id` **不是** 传输层 `fileId` 字符串本身，而是 **`SHA256(fileId 原始字节 ‖ chunkIndex 小端 u32)` 截断前 16 字节**（见 **`03-Storage.md` 第七节 `MM2` 行**）。`chunk_idx` 与 `chunkIndex` 一致；单次写入仍受 **`ZDB_MAX_WRITE_SIZE`** 约束（经 `ZdbManager::WriteData`）。

## 5. 与 `Types.h` 其它常量

- **`ZDB_MAX_WRITE_SIZE`**：`ZdbManager::WriteData` 单次 `length` 上限；**`MM2::StoreFileChunk`** 亦受此限（整段分片须一次写入）。
- **`ZDB_MIN_FILES` / `ZDB_MAX_FILES`**：仍为规范/预留常量；**当前 `ZdbManager` 未按个数强制启停分卷**（仅 `SelectFile` + 按需 `CreateNewFileUnlocked`）。

## 6. `ZdbManager` 行为摘要（与实现一致）

- **`file_id`**：磁盘上的**单层文件名**；实现拒绝含 `/`、`\`、`..` 的 `fileId`（防 `dataDir` 路径逃逸）。
- **`AllocateSpace`**：在管理器互斥锁内**写入全 0** 完成预留（非仅返回偏移），避免 TOCTOU。
- **并发**：`ZdbManager` 公开 API 由 `m_mutex` 串行化；`SqliteMetadataDb` **非**线程安全（见该头文件注释）。

## 7. 参考代码路径

- `include/mm2/storage/ZdbFile.h`、`src/mm2/storage/ZdbFile.cpp`
- `include/mm2/storage/ZdbManager.h`、`src/mm2/storage/ZdbManager.cpp`
