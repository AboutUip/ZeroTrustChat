# ZSP 网关—客户端实现契约

本文档规定 **TCP 上 ZSP 客户端** 与 **`ZChatServer`（Java / Netty）** 的**互操作字节级约定**。客户端实现**应以此与** `02-ZSP-Protocol.md` **联合为准**；网关可配置项以仓库内 `ZChatServer/src/main/resources/application.yml` 的 `zchat.zsp` 为参照。

---

## 一、适用范围与优先级

| 层级 | 说明 |
|------|------|
| 帧结构、MessageType、第六节载荷形态 | 以 [`02-ZSP-Protocol.md`](02-ZSP-Protocol.md) 为主 |
| 本文 **网关路由、Auth Tag 模式、SYNC 扩展、防重放、离线补发** | 以本文为准 |
| `userId` / `imSessionId` / `messageId` 等长度 | 以 [`06-Appendix/01-JNI.md`](../06-Appendix/01-JNI.md) 文首表为准（**16 字节**） |
| 与 `AUTHORITY.md` 冲突时 | 以 [`AUTHORITY.md`](../AUTHORITY.md) 为准 |

---

## 二、字节序与标识符

- **多字节整数**：一律 **大端（网络字节序）**。
- **ZSP Header 内 `SessionID`（4 字节）**：与 JNI / MM2 的 **`imSessionId`（16 字节）** 为**不同概念**，不得混写进同一字段语义。

---

## 三、Header.Flags（与网关行为）

| 位 | 名称 | 客户端义务 |
|----|------|------------|
| 0 | Compressed | **须为 0**。网关默认 **拒绝 Compressed=1**（断开），未实现解压。 |
| 1 | Encrypted | **1**：Payload 走 **§六 密文路由**；**0**：走 **§七 明文分项布局**（若某类型未在本文列出，则 opaque 透传 JNI，见网关实现）。 |
| 2–7 | 其余 | 按 [`02-ZSP-Protocol.md`](02-ZSP-Protocol.md) 3.1；未用位填 0。 |

---

## 四、Meta 与防重放

- **最小 Meta 长度**：**26** 字节；首 **uint16** 为 **Meta 总长度**，须等于 **实际 Meta 段字节数**（见 `02-ZSP-Protocol.md` 第四节）。
- **Timestamp**：Unix **毫秒**；与网关接收时刻之差的绝对值不得超过 **`replay-timestamp-window-minutes`**（默认 **5** 分钟），否则帧被拒绝。
- **Nonce（12 字节）**：同一 TCP 连接上**不得重复**（网关用集合近似防重放）。
- **Sequence（Header）**：单调递增；与网关**上一已处理序号**之差须在 **±500** 窗口内（对齐协议 8.4 精神）。

---

## 五、Auth Tag（16 字节）

协议第九节定义 **AEAD 输出**；在 **Java 网关**上支持下列 **部署模式**，**须与客户端一致**：

### 5.1 模式 `NONE`（默认）

- 收发 **16 字节全 0**。

### 5.2 模式 `HMAC_SHA256_128`

**非** AES-GCM；为**网关帧完整性**，与 E2E AEAD 可并存于产品策略，但**双方必须同模式、同密钥推导**。

| 项目 | 约定 |
|------|------|
| 共享密钥材料 | 部署配置 `frame-integrity-secret`（UTF-8 字符串） |
| 派生密钥 | `K = SHA-256(UTF-8(secret))`，长度 **32 字节** |
| 待认证数据 `M` | `Header(16) ‖ Meta ‖ Payload`（与线上一致：`Header` 编码见本文 **§5.3**） |
| 算法 | `HMAC-SHA256(K, M)` |
| Auth Tag | 上述 HMAC 输出 **前 16 字节** |

#### 5.3 Header 16 字节线上编码（用于 HMAC 输入）

与实现一致，字段顺序为：

| 偏移 | 字段 | 类型 |
|------|------|------|
| 0 | Magic | uint16 BE，值 `0x5A53` |
| 2 | Version | uint8 |
| 3 | MessageType | uint8 |
| 4 | Flags | uint8 |
| 5 | Reserved | uint8 |
| 6 | SessionID | uint32 BE（ZSP 头 4 字节会话，**非** 16B `imSessionId`） |
| 10 | Sequence | uint32 BE |
| 14 | Length | uint16 BE，等于 **Payload 长度** |

服务端在 **`verify-inbound-auth-tag=true`** 且模式为 `HMAC_SHA256_128` 时校验入站 Tag；客户端应对**出站**帧使用**相同**计算方式。

---

## 六、密文载荷（Encrypted=1）

网关**不解密** E2E 内层；仅按前缀拆分 **`imSessionId`**、可选 **`toUserId`**，并将 **body** 作为 opaque 传入 JNI `storeMessage`。

| `opaque-routing-min-bytes` | Payload 前缀语义 |
|---------------------------|------------------|
| **32**（默认） | `imSessionId(16) ‖ toUserId(16) ‖ body` |
| **16** | `imSessionId(16) ‖ body` |

- **`toUserId` 全 0**：不向其他在线连接转发。
- **非全 0**：若该 **userId** 在线，网关向其连接**再发一帧**（新 Meta/Seq/Tag，**MessageType 与 Payload 与原始请求相同**）。

---

## 七、明文载荷（Encrypted=0）— 网关分项约定

未列类型：网关可能 **整段 Payload** 作为 opaque 调用 JNI（以 `ZChatServer` 的 `ZspMessageDispatcher` 为准）。

### 7.1 IM 类（TEXT / IMAGE / VOICE / VIDEO / TYPING / CUSTOM 等）

- 推荐布局：`imSessionId(16) ‖ toUserId(16) ‖ body`。  
- **`toUserId` 全 0**：仅 `storeMessage`，不转发。  
- **非全 0**：`storeMessage` 且向对端**在线**连接转发（同 §六 转发语义）。

### 7.2 SYNC（0x83）

| 条件 | 布局 |
|------|------|
| **长度 ≥ 48** | `imSessionId(16) ‖ jniListUserId(16) ‖ lastMsgId(16) [‖ limit(4,uint32 BE)]` |
| **长度 ∈ [36, 47]**（兼容） | `imSessionId(16) ‖ lastMsgId(16) [‖ limit(4)]` |
| **长度 = 16** | 仅 `imSessionId` |

- **`lastMsgId` 为空或全 0**：调用语义等价于 **`getSessionMessages(caller, imSessionId, limit)`**。  
- **`lastMsgId` 非空**：扩展格式用 **`jniListUserId`** 作为 JNI `listMessagesSinceMessageId` 的 **userId** 参数；兼容格式用 **`imSessionId`** 作为该参数（产品若需区分会话键与列表 userId，**应使用扩展格式**）。

**响应 `SYNC` Payload**：`count(4,uint32 BE) ‖` 重复 `rowLen(4) ‖ rowBytes`。

### 7.3 AUTH（0x81）

`UserIDLen(2) ‖ UserID ‖ TokenLen(2) ‖ Token`。**UserID 须为 16 字节** 以满足 MM1 / JNI 校验。

### 7.4 好友（节选）

| 类型 | 布局 |
|------|------|
| FRIEND_REQUEST | `from(16) ‖ to(16) ‖ timestamp(8,uint64 BE) ‖ signatureEd25519(64)` |
| FRIEND_RESPONSE | `requestId(16) ‖ accept(1) ‖ responderId(16) ‖ timestamp(8) ‖ signatureEd25519(64)` |
| DELETE_FRIEND | `userId(16) ‖ friendId(16) ‖ timestamp(8) ‖ signatureEd25519(64)` |

### 7.5 群双 ID（GROUP_INVITE / GROUP_REMOVE / GROUP_LEAVE）

- `groupId(16) ‖ userId(16) ‖` 可选尾部；语义见 [`02-ZSP-Protocol.md`](02-ZSP-Protocol.md) 与 JNI。

### 7.6 GROUP_CREATE（0x0F）

- **Payload 仅为群名**：`nameLen(2,uint16 BE) ‖ nameBytes(UTF-8)`。**创建者** 为当前已认证用户（AUTH 的 16B `userId`），**不**在 Payload 内重复携带。群名长度上限与 JNI **`createGroup`** 一致（≤2048 字节 UTF-8 语义以 C++/文档为准）。

### 7.7 GROUP_NAME_UPDATE（0x1C）

- `groupId(16) ‖ updaterId(16) ‖ nameLen(2) ‖ name UTF-8`。

### 7.8 文件

- **FILE_CHUNK / FILE_COMPLETE**：见 `02-ZSP-Protocol.md` 第六节。  
- **RESUME_TRANSFER**：`fileIdLen(2,uint16 BE) ‖ fileId(UTF-8) ‖ chunkIndex(4,int32 BE)`。  
- **CANCEL_TRANSFER**：`fileIdLen(2,uint16 BE) ‖ fileId(UTF-8)`。

### 7.9 RTC

- **VOICE_CALL / VIDEO_CALL**：`peerUserId(16) ‖ callKind(4,int32 BE)`。  
- **CALL_SIGNAL**：`subType(1) ‖ duration(4) ‖ data`；**data 前 16 字节** 为 **callId**（与 `MESSAGE_ID_SIZE` 一致）。

---

## 八、心跳与读空闲

- 客户端建议每 **≤30 秒** 发送 **HEARTBEAT（0x80）**（与 [`04-Session.md`](../03-Business/04-Session.md) 一致）。  
- 网关 **读空闲** 默认 **90 秒** 断开（`reader-idle-seconds`）。  
- **`heartbeat-echo=true`** 时，服务端对 HEARTBEAT 回送 **同类型、空 Payload** 帧。

---

## 九、离线补发（可选）

- **`offline-queue-enabled=true`**：当 **转发目标不在线** 时，网关可将待转发业务**入内存队列**；目标用户 **下次 AUTH 成功** 后**按序下发**（仍为 ZSP 帧）。  
- **单用户队列长度** 上限由 `offline-queue-max-per-user` 约束；**进程重启队列丢失**。生产环境应使用 **Redis 等** 替换或补充。

---

## 十、TLS

- 可在 TCP 上叠加 **TLS**；**不改变** ZSP 帧内字段语义（见 `02-ZSP-Protocol.md` 第十节）。

---

## 十一、配置键对照（`zchat.zsp`）

| 键 | 含义（客户端需感知的行为） |
|----|---------------------------|
| `port` | TCP 监听端口（默认 8848） |
| `heartbeat-echo` | 是否回显 HEARTBEAT |
| `replay-protection-enabled` | 是否启用序列号 + nonce（+ 时间窗）校验 |
| `require-encrypted-flag` | 是否强制 Encrypted=1 |
| `opaque-routing-min-bytes` | **16 或 32**，见 §六 |
| `frame-tag-mode` | `NONE` 或 `HMAC_SHA256_128`，见 §五 |
| `frame-integrity-secret` | HMAC 模式共享密钥材料（部署注入） |
| `verify-inbound-auth-tag` | 服务端是否校验入站 Tag（客户端 HMAC 须一致） |
| `replay-timestamp-window-minutes` | Meta 时间戳允许偏差 |
| `reject-compressed-payload` | Compressed 必须为 0 |
| `offline-queue-enabled` / `offline-queue-max-per-user` | 见 §九 |

---

## 十二、版本与变更

- **协议版本**：`Header.Version = 0x01`（与 `02-ZSP-Protocol.md` 一致）。  
- **本文档**变更时，须同步 **客户端** 与 **网关** 发布说明；若仅网关默认配置变化，须在运维文档中注明。
