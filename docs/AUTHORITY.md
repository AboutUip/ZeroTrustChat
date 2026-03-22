# 规范权威与持久化

## 1. 冲突时的优先顺序

| 优先级 | 依据 |
|:------:|------|
| 1 | `ZChatIM/` 源码 |
| 2 | [`02-Core/03-Storage.md`](02-Core/03-Storage.md) 第七节、[04-ZdbBinaryLayout.md](02-Core/04-ZdbBinaryLayout.md) |
| 3 | [`ZChatIM/docs/Implementation-Status.md`](../ZChatIM/docs/Implementation-Status.md) |

## 2. 持久化原则

落盘扩大暴露面：仅允许经审定的数据进入 SQLite / `.zdb`；默认最小化；优先内存。

## 3. 实现摘要（与源码一致）

| 类别 | 行为 |
|------|------|
| IM 载荷 | `StoreMessage` 密文驻留进程内 RAM；元库无 `im_messages` / `im_message_reply`；进程结束即失 |
| 文件分片、好友/群元数据、UserData、设备会话等 | MM2 + SQLCipher 元库 + `.zdb`，见 Storage 第七节 |
| MM1 会话、认证限流 | 进程内状态，与上表磁盘语义分开描述 |

硬限制（与 `Types.h`、Zdb 布局一致）：`FILE_CHUNK_SIZE` 64KiB；`ZDB_FILE_SIZE` 5MiB；`ZDB_MAX_WRITE_SIZE` 500KiB。

## 4. C++ 交付判定

`ZChatIM --test` 通过；[`Implementation-Status.md`](../ZChatIM/docs/Implementation-Status.md) 第七节与 [`01-MM1.md`](02-Core/01-MM1.md) 一点五.3 所列边界满足。

## 5. 行文

章节称「第×节」。勿使用 U+00A7。持久化或 IM 语义变更须同步 Storage、Implementation-Status、相关规范与头文件。
