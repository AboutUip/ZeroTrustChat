# 文件传输技术规范

> **冲突处理**：**`StoreFileChunk`** 将分片写入 **`.zdb`** 并记 **`data_blocks`**（**`03-Storage.md` 第七节**），**非**「仅内存」。**续传断点**由 **`MM2::StoreTransferResumeChunkIndex` / `GetTransferResumeChunkIndex` / `CleanupTransferResumeChunkIndex`** 写入 SQLite 表 **`mm2_file_transfer`**（**`user_version=4`**，见 **`05-ZChatIM-Implementation-Status.md` 第2.1节**）；**`CompleteFile` / `CancelFile`** 已接库。**JNI** 侧仍待 **`ZChatIMJNI`** 导出（**`01-JNI.md`**）。

## 一、传输流程

```
发送方:
1. 加密文件 → 分片 (每片 ≤64KB)
2. 发送 FILE_INFO → FILE_CHUNK × N → FILE_COMPLETE
3. 每片独立加密 (独立Nonce)
4. SHA256 校验

接收方:
1. 接收 FILE_INFO
2. 依次接收 FILE_CHUNK
3. 验证 SHA256
4. 完整文件解密
```

## 二、分片结构

```
┌─────────────────────────────────────────────────────────────┐
│  FILE_INFO (0x05)                                           │
├─────────────────────────────────────────────────────────────┤
│  FileID: UUID                                               │
│  FileName: 文件名                                            │
│  FileSize: 文件大小                                          │
│  SHA256: 完整文件哈希                                         │
│  EncryptKey: 文件加密密钥 (32B)                               │
│  ChunkSize: 分片大小                                         │
│  TotalChunks: 总片数                                         │
│  TransferMode: 0=直传 1=中转                                 │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│  FILE_CHUNK (0x06)                                          │
├─────────────────────────────────────────────────────────────┤
│  FileID: UUID                                               │
│  ChunkIndex: 当前片序号                                       │
│  ChunkData: 加密数据 (≤64KB)                                 │
└─────────────────────────────────────────────────────────────┘
```

## 三、续传机制

**已落盘部分**：已成功 **`StoreFileChunk`** 的分片在 **`.zdb`** 中，**进程重启后仍在**（路径见 **`MM2::Initialize`** 的 `dataDir`）。

**断点索引（最后成功 chunk）**：

- **MM2**：**`StoreTransferResumeChunkIndex`** 等已写入 **`mm2_file_transfer`**；**`GetTransferResumeChunkIndex`** 无行时 **`false`**（JNI 接线时常映射 **`UINT32_MAX`**，见 **`01-JNI.md`** 路由摘要）。
- **JNI**：仍待桥接；业务若只走 Java 且未接 native，则仍须**自存断点**或从零重传。

```
传输中断（目标流程）:
1. 记录最后成功 ChunkIndex（依赖实现的断点存储）
2. 重连后 RESUME_TRANSFER
3. 从断点继续发送；已落盘分片可由 GetFileChunk 读出
```

## 四、中断处理

| 场景 | 处理 |
|------|------|
| 网络中断 | 客户端重连 → 需重新发送 |
| 超时 | 30秒无数据 → 断开 |
| 验证失败 | 重发该片 (最多3次) |
| 放弃 | 发送 CANCEL_TRANSFER |
| 服务重启 | **已写分片保留**；**断点在 `mm2_file_transfer`**（与库同路径）**保留**，除非 **`CancelFile` / `CleanupTransferResumeChunkIndex` / 删库** |

## 五、安全保证

- 独立密钥: 每文件独立加密密钥
- 独立Nonce: 每分片独立Nonce
- 完整性: SHA256校验
- 存储: 分片经 **MM2 落 `.zdb`**；删除策略由业务（如 Complete/Cancel）与清理 API 决定
- 续传: **断点**依赖将实现/应用层；**已接收分片**不因重启自动消失
