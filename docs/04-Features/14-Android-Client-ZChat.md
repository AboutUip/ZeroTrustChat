# Android 参考客户端（ZChat）

> **定位**：仓库内 **ZSP 明文单聊** 的 Android 参考实现，用于联调网关与协议；**非** ZChatIM JNI 宿主。  
> **源码**：`Client/Android/`。权威协议仍以 [`../01-Architecture/02-ZSP-Protocol.md`](../01-Architecture/02-ZSP-Protocol.md) 为准。

## 1. 运行时与连接

- **单 TCP**：`ZspSessionManager` 维护一条 ZSP 连接；请求与入站 `MESSAGE_TEXT` 共用该连接。
- **认证**：本地密码认证后与网关交互；会话级 IM 使用 `PeerImSession.deriveSessionId` 派生 16 字节会话 ID。
- **入站 TEXT**：`ZChatApplication` 注册 `IncomingTextListener`，在单线程执行器中先处理语音相关逻辑，再触发 `ChatSync`（见下文）。

## 2. 明文聊天与本地库

- 发送：`ZspChatWire.buildTextPayload`；接收：`parseSyncResponse` 解析 `plainPayload`（`toUserId(16) ‖ UTF-8`）。
- **本地 SQLite**：`ChatMessageDb` 按对方 `peer_hex` 存消息，`message_id` 与服务端 16 字节 ID 对应，用于去重与引用回复。
- **不计入聊天的前缀**：以 `__ZRTC1__` 开头的 WebRTC 信令、通话流水格式（见 `ChatCallLogHelper`）在同步入库路径中跳过或单独处理。

## 3. 消息同步（SYNC）客户端策略

服务端按会话内消息顺序提供 **`ListMessagesSinceMessageId(sessionId, lastMsgId, limit)`** 语义；客户端增量载荷见 `ZspChatWire.buildSyncPayloadSince`。

本客户端为避免 **游标错误** 与 **历史重放**：

| 策略 | 说明 |
|------|------|
| **最新游标** | `getLastMsgId16ForPeer` 使用 **`ORDER BY id DESC`**（行自增 id），**不**使用 `ts_ms` 最大值作为 `lastMsgId`（插入时间不等于服务端链序）。 |
| **最早游标** | `getOldestMsgId16ForPeer` 使用 **`ORDER BY id ASC`**，对 `syncSince(oldest)` 向前补缺；若整页均为已存在重复则停止，避免死循环。 |
| **尾部追新** | 多轮 SYNC，直至返回不足一页或本轮无新插入。 |
| **首窗合并（可选）** | `mergeServerHeadWindow=true` 时先请求 **initial**（仅 im session 16 字节）拉取会话 **前 N 条**，用于修复「仅持有尾部消息 id」导致的中间空洞。用于：进入聊天首次同步、下拉刷新全会话、FCM/推送唤醒、实时入站前与首窗配合等（调用点见源码）。 |
| **单轮内时间戳** | 同一轮多次插入使用单调递增 `ts_ms`，避免多批同毫秒导致列表顺序不稳定。 |

与 **§ 消息同步机制** 总述的关系：见 [`01-MessageSync.md`](01-MessageSync.md) 第七节。

## 4. 语音与 WebRTC 信令

| 项 | 说明 |
|----|------|
| **前缀** | `__ZRTC1__` + JSON（`WebRtcSignaling`）。 |
| **媒体** | `WebRtcAudioCallSession` + `io.github.webrtc-sdk:android`；ICE 服务器在会话内配置（含 STUN/TURN；生产环境宜改为可配置自有 TURN）。 |
| **实时信令** | 收到 ZSP `MESSAGE_TEXT` 全帧后，**先于** `ChatSync.scheduleSyncFromIncomingPush` 调用 `VoiceCallEngine.dispatchSignalingFromIncomingTextPayload`，保证首包 `offer` 等不被 SYNC 时序吞掉。 |
| **SYNC 中的信令** | 历史批次会反复带上会话内旧信令。若在 **IDLE** 下仍对 **offer** 调用与「新来电」相同的逻辑，会在挂断/失败后再次弹出来电界面。**实现**：`ChatSync` 在 **仅当** `VoiceCallCoordinator.canAcceptIncomingOffer()` 为真且信令为 **offer** 时**不**派发；非 IDLE 或非标 offer（answer/ice/bye 等）仍派发，以支持进行中会话与重协商。 |
| **全局状态** | `VoiceCallCoordinator` 相斥；`VoiceCallForegroundService`、通知与 `VoiceCallActivity` 协同。 |

## 5. 引用回复

- 与 [`10-MessageReply.md`](10-MessageReply.md) 对齐；客户端使用 `ChatReplyCodec` 编解码，UI 长按气泡菜单「回复」等（实现以源码为准）。

## 6. 推送与后台

- **FCM**：用于唤醒与提示；具体数据字段与 `ZChatFirebaseMessagingService` 实现一致。
- **WorkManager**：如好友请求轮询等（见 `FriendRequestPollWorker`）。

## 7. 已知限制与后续方向

- **离线首包 offer**：若进程未收到 TCP 帧、仅靠后续 SYNC 才第一次看到 `offer`，当前为防重放**可能**不再从 SYNC 路径拉起全屏来电；完整离线来电需配合推送载荷或业务策略单独设计。
- **TURN**：公共演示 TURN 仅供联调；生产应使用自有或商业 TURN，并支持配置下发。
- **证书固定**：若产品要求，见 [`07-CertPinning.md`](07-CertPinning.md)。

## 8. 变更记录（维护说明）

重大行为变更（SYNC 轮次、信令路径、WebRTC 依赖坐标）须同步：本文件、[`01-MessageSync.md`](01-MessageSync.md) 第七节、`Client/Android/README.md`。
