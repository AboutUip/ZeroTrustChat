# `.zdb` 容器二进制布局（v1）

本文档描述 **MM2** 中 `.zdb` 文件的 **v1** 物理格式，与 `ZChatIM/include/Types.h` 中的 `ZdbHeader`（`ZChatIM_ZdbHeaderLayout`）、`MAGIC_ZDB`、`ZDB_FILE_SIZE` 及实现 `ZdbFile` / `ZdbManager` 对齐。

## 1. 设计要点

| 项 | v1 行为 |
|----|---------|
| 文件总长 | 固定 **`ZDB_FILE_SIZE`**（当前 **5 MiB**，与 `Types.h` 一致） |
| 头部 | **64 字节** `ZdbHeader`，其后为 **payload** 区 |
| 写入路径 | **仅尾部追加**（`AppendRaw` / `ZdbManager::WriteData`）；**无加密**、无空闲块回收表 |
| 头部校验域 | `checksum[32]` 预留，v1 填 **0** |
| 尾部 | 创建时头之后至文件尾预填 **0x00**，便于十六进制查看 |

## 2. `ZdbHeader`（64 字节，`#pragma pack(push, 1)`）

所有多字节整数均为 **小端（LE）**。下列 **偏移** 自文件开头。

| 偏移 | 长度 | 类型 / 含义 |
|------|------|-------------|
| 0x00 | 4 | **`magic`**：`MAGIC_ZDB` = `'Z' 'D' 'B' '\0'`（`0x5A 44 42 00`） |
| 0x04 | 1 | **`version`**：v1 = **1** |
| 0x05 | 7 | **`reserved`**：v1 填 **0** |
| 0x0C | 4 | **`padding`**：v1 填 **0** |
| 0x10 | 8 | **`totalSize`**：`uint64_t`，等于文件总长度（通常为 `ZDB_FILE_SIZE`） |
| 0x18 | 8 | **`usedSize`**：`uint64_t`，**已用逻辑长度**；含本 64 字节头，即 payload 从 **`usedSize` 上一次追加结束位置** 开始增长；初始创建后为 **64** |
| 0x20 | 32 | **`checksum`**：v1 全 **0** |

`static_assert(sizeof(ZdbHeader) == 64)`（见 `ZdbFile.cpp`）。

## 3. Payload 区

- **有效范围**：`[64, totalSize)`。
- **第一条追加记录**起点偏移为 **64**（此时创建后 `usedSize == 64`，追加后 `usedSize == 64 + length`）。
- 读取/校验时，**元数据**（`SqliteMetadataDb` / `StorageIntegrityManager`）中的 **`offset` / `length`** 指 payload 区内字节范围（可与头64分开存，也可从 64 起算，须与写入约定一致；当前自检用 **`offset == 64`** 表示首条 payload）。

## 4. 与 SQLite 索引的关系

- `zdb_files`：`file_id` 与磁盘文件名一致；`total_size` / `used_size` 应与头内 `totalSize` / `usedSize` 语义一致（由上层在写入后维护）。
- `data_blocks`：`data_id`（16 字节）、`file_id`、`offset`、`length`、`sha256` 描述一块 payload；**v1** 不加密时 `ComputeSha256` 可直接针对该区间。

## 5. 参考代码路径

- `include/mm2/storage/ZdbFile.h`、`src/mm2/storage/ZdbFile.cpp`
- `include/mm2/storage/ZdbManager.h`、`src/mm2/storage/ZdbManager.cpp`
- 自检：`ZChatIM --test` 中 **ZDB v1** 相关用例（见 `tests/mm1_managers_test.cpp`）。
