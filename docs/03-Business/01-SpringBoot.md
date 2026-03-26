# SpringBoot 技术规范

> **仓库边界**：**`ZChatIM/`** 提供 **MM1/MM2 + JNI 头 + `jni/ZChatIMJNI.cpp`（`JNI_OnLoad` → `RegisterNatives`）**；**SpringBoot/Netty** 工程若在本 monorepo 外，须与本节及 **`02-ZSP-Protocol.md`** 对齐。  
> **JNI 契约**：**不得以本节 第五节 简表替代** **`docs/06-Appendix/01-JNI.md`**（含 **`callerSessionId` 首参**、`imSessionId` 与 **`StoreMessage`** 参数名）。  
> **落盘**：**IM** 默认**进程内 RAM**（**`MM2::StoreMessage`**，见 **`docs/AUTHORITY.md`**）；**文件分片 / 好友请求 / 群元数据等**由 **MM2 + 元库 / `.zdb`** 持久化；SpringBoot **不直连** SQLite/`.zdb` 文件。

## 一、职责

- **ZSP 协议服务端**（**自研应用层协议**，见 **`02-ZSP-Protocol.md`**；**非**以 HTTPS 为 IM 主语义）
- 业务逻辑调度
- 消息路由
- 可选 **对外 HTTP API**（运维、管理端等；**与 ZSP 主链路正交**）

**原则**：不处理任何安全相关数据，仅持有引用 ID；**Payload opaque 透传** 至 JNI。

详见 [01-Overview.md](../01-Architecture/01-Overview.md)。**客户端与网关字节约定**（Auth Tag 模式、密文路由、SYNC 扩展、防重放等）以 **[03-ZSP-Gateway-Client-Contract.md](../01-Architecture/03-ZSP-Gateway-Client-Contract.md)** 为准。

---

## 二、网络架构

```
┌─────────┐      ZSP      ┌─────────────┐
│  客户端  │ ────────────> │ SpringBoot  │
│         │ <───────────  │  (Netty)    │
└─────────┘               └──────┬──────┘
                                 │
                                 │ JNI
                                 ▼
                         ┌─────────────┐
                         │     C++     │
                         │   (MM1/MM2) │
                         └─────────────┘
```

---

## 三、模块划分

### 3.1 ZSP Server

| 模块 | 说明 |
|------|------|
| ZSPDecoder | 协议解码 |
| ZSPEncoder | 协议编码 |
| ZSPHandler | 消息分发 |
| HeartbeatHandler | 心跳检测 |

### 3.2 Business

| 模块 | 说明 |
|------|------|
| ConnectionManager | 连接管理 |
| MessageRouter | 消息路由 |
| SessionManager | 会话管理 (仅存 ID) |
| UserManager | 用户管理 |

### 3.3 JNI Client

| 模块 | 说明 |
|------|------|
| NativeInterface | JNI 调用封装 |
| ResultHandler | 结果处理 |

---

## 四、消息处理

### 4.1 接收消息

```
1. Netty 接收 ZSP 数据
2. ZSPDecoder 解析 Header + Meta
3. 获取 MessageType、ZSP 层 SessionID（4B 头字段，与 imSessionId 16B 不同，见 01-JNI.md）
4. 调用 JNI: 已认证上下文中 storeMessage(caller, imSessionId, payload)（完整签名见 01-JNI.md）
5. 获取 msgId
6. 路由发送
```

### 4.2 发送消息

```
1. 根据连接查找目标 Channel
2. 调用 JNI: retrieveMessage(caller, messageId)（或 getSessionMessages 等）
3. 获取 opaque 载荷
4. ZSPEncoder 编码
5. 发送
```

---

## 五、JNI 接口

| 接口 | 输入 | 输出 | 说明 |
|------|------|------|------|
| `auth` | userId, token, clientIp(可选) | sessionId/false | 认证；IP 建议透传至 JNI 以满足 02-Auth |
| `storeMessage` | sessionId, data | msgId/null | 存储消息 |
| `retrieveMessage` | msgId | data/null | 获取消息 |
| `getSessionStatus` | sessionId | active/invalid | 会话状态 |
| `destroySession` | sessionId | true/false | 销毁会话 |
| `getUserStatus` | userId | online/offline | 用户状态 |
| `emergencyWipe` | - | - | 紧急销毁 |

---

## 六、安全原则

| 禁止事项 | 说明 |
|----------|------|
| 不存储消息明文 | 仅透传 |
| 不处理加解密 | 由 C++ 处理 |
| 不存储密钥 | 不获取不存储 |
| 不解析业务数据 | Payload 原文传递 |

---

## 七、错误处理

| 场景 | 处理 |
|------|------|
| JNI 调用失败 | 返回错误码 |
| sessionId 无效 | 断开连接 |
| 目标不在线 | **离线队列 / 推送**（产品）；**非**「SpringBoot 写 MM2」——持久化仅在 **native MM2** 路径完成 |
| 超时 | 重试/断开 |

**网关侧离线队列（`ZChatServer`）**：实现为**进程内内存队列**（`zchat.zsp.offline-queue-*`），适用于**单实例**；进程重启或扩容多实例时的语义由部署与后续产品版本约定，**不依赖** Redis 等外部组件作为 Java 网关交付前提。

---

## 八、日志与可观测

### 8.1 原则（防泄露）

- **不向外部日志平台 / 文件采集管道批量导出**含业务上下文的运行日志（与「持久化扩大泄露面须收紧」一致）。
- 网关 **不记录**：消息内容、密钥、令牌、可还原身份的标识；**不将**对端可控字符串（如异常 `getMessage()`）写入日志。
- **认证路径**：与 [02-Auth.md](02-Auth.md) 一致，**认证计数 / 封禁等不记日志**；网关侧对 **AUTH 失败**亦仅使用**固定原因码**（在开启诊断时），不记录用户标识或载荷。
- **排障**：生产默认关闭 ZSP 诊断日志；需要时短时开启 `zchat.zsp.diagnostic-logging=true`，仍只输出**固定码**（如 `frame_validation_failed`），不含载荷。长期可依赖 **Micrometer 等指标**（计数器、无 PII），与日志导出解耦。

### 8.2 配置（`ZChatServer`）

| 配置项 | 默认 | 说明 |
|--------|------|------|
| `zchat.zsp.diagnostic-logging` | `false` | 为 `true` 时输出 ZSP 路径上上述固定码；为 `false` 时不写入这些诊断行。 |
| `zchat.zsp.frame-tag-mode=HMAC_SHA256_128` | — | 须同时配置非空 `zchat.zsp.frame-integrity-secret`，否则**进程启动失败**，避免运行期误配。 |
| `spring.security.user.*` / `ZCHAT_ACTUATOR_USER` / `ZCHAT_ACTUATOR_PASSWORD` | 见 `application.yml` | 仅保护 **`/actuator/**`**（HTTP Basic）；生产须改密，见 §8.3。 |
| `ZCHAT_HTTP_BIND_ADDRESS` | `0.0.0.0`（未设置时） | 绑定 HTTP（含 Actuator）监听地址；本机运维可设为 **`127.0.0.1`**，与防火墙/TLS 配合。 |
| （JNI） | 必选 | 启动时加载 **`ZChatIMNative`** 并 **`initialize`**；须配置 **`zchat.zsp.native.data-dir`** / **`index-dir`**（默认 **`${user.dir}/target/zchat-mm-data`** 等，勿依赖 **`java.io.tmpdir`** 在 Windows 上含中文路径）；可用环境变量 **`ZCHAT_MM_DATA_DIR`** / **`ZCHAT_MM_INDEX_DIR`** 覆盖。失败则进程不启动。若 MM2 需要口令则配置 **`zchat.zsp.native.passphrase`**（与 `initializeWithPassphrase` 对齐）。 |
| JDK 24+ 运行主类 / JNI | — | 若出现 `System::load` restricted 警告，在 **IDEA Run → VM options** 或 **`mvn spring-boot:run`**（见 `pom.xml` profile `jdk24-jvm-warnings`）增加 **`--enable-native-access=ALL-UNNAMED`**（及按需 **`--sun-misc-unsafe-memory-access=allow`**，与 Netty 文档一致）。 |

Netty **监听/停止**等生命周期 INFO 仍可能出现在进程标准输出；若需完全静默，由部署侧重定向或统一日志策略处理，**不在此规范强制**。

### 8.3 Actuator（健康与指标）

- **与 IM 无关**：Actuator 走 **HTTP**（`server.port`，默认与 ZSP TCP `zchat.zsp.port` 分离），**不参与** ZSP 帧与聊天业务；仅用于进程级**健康与性能指标**。
- **`/actuator/health`**：聚合进程健康；ZSP 网关由 `ZspHealthIndicator` 贡献 **`zsp`** 详情（`disabled` / `listening` + 监听端口 / `not_listening`），**不含**用户或会话标识。
- **`/actuator/metrics`**、**`/actuator/prometheus`**：Micrometer / Prometheus 文本；**仅**计数与 Gauge、关闭原因等标签，**不**包含消息载荷或身份字段。生产保持 `management.endpoint.health.show-details=when_authorized`（见 `application.yml`），且 **`management.endpoints.web.exposure.include`** 仅包含运维所需端点（如 `health`、`metrics`、`prometheus`），**勿**开放 `env`、`heapdump` 等高危端点。
- **内嵌运维面板**：`http://<host>:<port>/ops`（重定向至 **`/ops/index.html`**）为静态页面，浏览器 **HTTP Basic** 与 Actuator **相同账号**；页面内拉取 **`/actuator/health`**、**`/actuator/metrics/*`** 展示摘要。与 ZSP 无关。生产须 HTTPS、内网或反向代理。
- **访问控制**：`/actuator/**` 与 **`/ops/**`** 使用 **HTTP Basic**，凭据为 `spring.security.user.name` / `spring.security.user.password`（建议用环境变量 **`ZCHAT_ACTUATOR_USER`** / **`ZCHAT_ACTUATOR_PASSWORD`** 注入，**禁止**在生产使用仓库默认口令）。其余 HTTP 路径默认 **403**，避免误暴露。Prometheus 抓取须在采集侧配置 Basic Auth。
- **传输安全**：规范**不**将 IM 绑定为 HTTPS；Actuator 若暴露于非信任网络，须在**前置反向代理终止 TLS**，或配置进程内 TLS（`server.ssl.*` / Spring Boot 3 ssl bundle），**禁止**明文跨公网传密码。
- **绑定地址**：`server.address` 默认 `0.0.0.0`（见 `application.yml`），可通过 **`ZCHAT_HTTP_BIND_ADDRESS`** 收窄到本机（如 `127.0.0.1`）；**不替代**认证与 TLS。

### 8.4 历史表述（兼容）

- 操作类日志若存在，须遵守 8.1；**默认以「不导出、不记录敏感字段」为准**。

---

## 九、JNI 与 Spring 联调测试

- **DLL 位置**：**`ZChatServer/src/main/resources/native/windows-x64/`**（与 classpath **`/native/windows-x64/`** 一致）或 **`native/`** 根目录；由 `NativeLibraryLoader` 加载。联调类通过 **`JniItSupport`** 按上述路径自动设置 `zchat.native.dir`；其它路径仅用 **`ZCHAT_NATIVE_DLL_DIR`** 或 **`-Dzchat.native.dir`**。
- **何时跑**：IDEA 内运行（classpath 含 `idea_rt.jar`）或 Maven 加 **`-Dzchat.it.jni.enabled=true`**；否则跳过联调类。
- **`ZChatImJniInterop200Test`** / **`ZChatNativeSpringIntegrationIT`**：`validateJniCall*` 与 Spring 注入的 **`ZChatNativeOperations`** 烟测。
