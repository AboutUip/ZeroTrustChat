# 工程范围与非交付项

与 [`Implementation-Status.md`](Implementation-Status.md) 分工：本文述阶段边界与排除项；该文述模块状态与勾选。

## 1. M0：契约与工程

| 项 | 要求 |
|----|------|
| JNI 变更 | 同步 `docs/06-Appendix/01-JNI.md`、`JNI-API-Documentation.md`、`Implementation-Status.md` 及 `JniInterface.h` / `JniBridge.h` |
| JNI 堆缓冲 | `JniSecurity::AllocateJniMemory` / `FreeJniMemory` → `common::Memory`（`include/mm1/JniSecurity.h`） |
| 回归 | `ZChatIM --test` 通过 |

## 2. M1：数据面

多设备、在线缓存、证书 Pin、@ALL 等见 `docs/04-Features/` 与 `docs/02-Core/03-Storage.md`（`user_version=11`）。端到端联测（含重启）在 Android 与 Spring 就绪前以 `--test` 为回归依据。

## 3. M2：密钥与部署

**前提**：设备列表、踢设备、登录态以服务端为权威；`mm1_device_sessions` 等为客户端缓存。

**本期交付**：ZMK1（Windows DPAPI）、ZMK2（Unix）、ZMK3（Apple Keychain）保护 `indexDir/mm2_message_key.bin`；ZMKP v1（`MM2::Initialize(..., passphrase)`、`initializeWithPassphrase`）；SQLCipher 密钥与消息主密钥域分离（`docs/02-Core/03-Storage.md` 第4.2节）。

**非本期**（须单独立项）：HSM/TEE 托管主密钥或 SQLCipher key；libsecret/Credential Manager 替代 ZMK；ZMKP→ZMK 自动迁移；MM1 与 MM2 主密钥策略合并叙事。

运维：`docs/02-Core/03-Storage.md` 第4.3节。

## 4. M3：协议与密码学大项（非本期）

双棘轮、Random Allocator、纯内存 IM 等见 `docs/02-Core/01-MM1.md` 一点五.3；不纳入当前 `--test` 门槛。

## 5. 阻塞与外部依赖

| 项 | 说明 |
|----|------|
| 端到端联测 | 见 `Implementation-Status.md` 第7.1节 |
| JNI/Java | `JniNatives.cpp` 中 `kNativeMethods` 与 `ZChatIMNative.java` 须一致，否则 `JNI_OnLoad` 失败（`docs/06-Appendix/01-JNI.md` 文首） |
| ZSP 网关 | 本仓库交付 Core+JNI；编码对齐 `docs/01-Architecture/02-ZSP-Protocol.md` |

## 6. 已知简化

| 项 | 说明 |
|----|------|
| `BlockIndex` | 未使用；索引由 `SqliteMetadataDb` / `data_blocks` |
| `SideChannel` | 部分为尽力实现；敏感比对以 `ConstantTimeCompare` / OpenSSL 为准 |
| JNI 媒体 | 无采集与 WebRTC；仅呼叫状态，信令走 ZSP |
