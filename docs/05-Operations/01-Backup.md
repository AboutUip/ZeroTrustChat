# 灾备恢复技术规范

> **冲突处理（必读）**：**`docs/README.md`**「冲突与权威」。**已写入磁盘**的 **`.zdb` 文件分片与元库行** 不会因进程崩溃而凭空消失；**IM（`StoreMessage`）不落盘**，重启即失，须靠**同步/服务端**补齐。

## 一、设计原则

```
安全 > 可用性
零备份 → 零泄露入口（产品策略；与本地落盘防丢失不矛盾）
```

## 二、进程崩溃 / 重启恢复（ZChatIM 客户端）

```
1. 重新启动进程 → MM1 会话丢失，客户端须重新 Auth
2. MM2 Initialize(dataDir, indexDir) → 打开已有 .zdb 与 zchatim_metadata.db
3. **`StoreFileChunk`** 等已写入的数据仍在 **`.zdb`/元库**，可继续 **`GetFileChunk` / 读元数据**；**IM** 须重新 **`StoreMessage`** 或由上层同步拉取（RAM 已空）
4. 若 index 与 .zdb 不一致，见 03-Storage 第七节 / 05-ZChatIM-Implementation-Status 第8节 部分失败路径
```

## 三、数据状态

| 数据 | 崩溃 / 重启后 |
|------|----------------|
| 用户/好友/群组等 **已持久化到 MM2 路径** 的元数据 | **保留**（在库内且文件未删） |
| **IM 消息密文**（`StoreMessage`） | **不保留**（**仅 RAM**；进程结束即失，须服务端/同步重建） |
| **文件分片**（`StoreFileChunk`） | **保留** |
| MM1 **会话、限流、封禁计数** | **丢失**（内存） |
| 协议 **Session Key**（内存） | **丢失**（须按 KeyRotate 重协商） |

## 四、客户端处理

```
- 本地消息：优先从 MM2 读取；同步仍可通过 ZSP 增量补齐
- 自动重连 → 重新 Auth → 再调业务 API
- 「消息已清除」仅在使用 CleanupAllData / 删数据目录 / 显式删除 后出现
```

## 五、安全性

- 无备份文件（产品策略）
- 无恢复接口（产品策略）
- **Level 2**：针对 **内存与策略性覆写**；**MM2 删除 IM 消息** 为 **`ImRamEraseUnlocked`**（RAM），**不**动文件分片的 **`data_blocks`**（见 **02-MessageRecall**、**03-Storage 第七节**、**05 第8节**）
