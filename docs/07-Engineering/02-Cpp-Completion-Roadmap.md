# ZChatIM C++ 收尾路线图

**作用**：把「彻底搞定 C++」拆成**可验收阶段**，避免与 **`01-MM1.md` 愿景文档**（双棘轮、纯内存消息等）混为一谈——后者与当前 **`MM2` 持久化** 产品路径并存时，以 **`docs/README.md` 权威链** + **[05-ZChatIM-Implementation-Status.md](../02-Core/05-ZChatIM-Implementation-Status.md)** 为准。

---

## 1. 当前基线（你已基本在此）

- **`ZChatIMCore`**：MM2 编排 + SQLCipher 元数据 + `.zdb` v1 + 主要 MM1 Manager 实装 + **`JniBridge`/`JniInterface`** 全表接线。
- **`ZChatIM --test`**：自检全绿为**回归门槛**。
- **已知缺口**：见 **`05` 第3节、第7.3–7.4节** 与下文阶段划分。

---

## 2. 阶段 M0 — 契约与工程闭环（短周期）

| 项 | 说明 |
|----|------|
| 文档与 **`05`** 同步 | 接口/行为变更时更新 **`01-JNI.md`**、**`ZChatIM/docs/JNI-API-Documentation.md`**、**`05`**。 |
| **[x] JNI 辅助内存** | **`JniSecurity::AllocateJniMemory` / `FreeJniMemory`** 已委托 **`common::Memory::Allocate` / `Free`**（分配成功 **`SecureZero`**）；见 **`ZChatIM/include/mm1/JniSecurity.h`**、**`MM1_security_submodules.cpp`**。 |
| **`SideChannel`** | 延时/缓存冲刷等已在 **`MM1_security_submodules.cpp`** 落地（有界工作量 + **x86 `CLFLUSH` 尽力**）；极端合规场景仍可加平台专项。 |
| **`SecurityMemory::IsLocked`** | 已与成功 **`Lock`** 的登记区间对齐；紧急擦除走 **`ReleaseAllLockTracking`**。 |

**验收**：构建 + **`--test`**；**`05`** 与源码一致。

**集成联测（M1 验收）— 当前阻塞**：**Android 客户端**与 **Spring Boot** 侧**未就绪**，**暂不开展**端到端联测；**C++ 回归**仍以 **`ZChatIM --test`** 为准。说明见 **`docs/README.md` 第2节**、**`05-ZChatIM-Implementation-Status.md` 第7.1节**。

**M2 最小落地（第4节）— 工程对照**：上线/对接服务端前，按 **[03-M2-Minimal-ServerOwnedDevices.md](03-M2-Minimal-ServerOwnedDevices.md)** 核对 **设备权威在服务端**、本地 **`mm1_device_sessions`** 仅为缓存等；与 **`03-Storage.md` 第4.2 / 4.3节** 一致即可，**不依赖**联测环境就绪。

**M3**：**须独立立项**；**不得**与 M0/M1 补洞迭代混排。见 **第5节**、**`01-MM1.md` 一点五.3**、**`README.md` 第2节**。

---

## 3. 阶段 M1 — 数据面「产品级」补齐（按产品选择）

| 项 | 说明 |
|----|------|
| **多设备 / 在线 / Pin 等** | **`DeviceSessionManager` / `SessionActivityManager` / `CertPinningManager` / `UserStatusManager`** 与 **@ALL 限速** 已落 **`SqliteMetadataDb`**（**`user_version=11`**：`mm1_device_sessions` / **`mm1_user_status`** / **`mm1_mention_atall_window`** 等）。**`06-MultiDevice.md` / `07-CertPinning.md`** 语义保持。 |
| **群友图 / 可见性** | **群会话**下 **`StoreMessageReplyRelation`** 已由 **`MM2` + `group_members`** 做 **SQL 成员校验**；其它提及/可见性路径仍可按产品扩展。 |

**验收**：产品场景清单 + 集成测试（含**进程重启**）。

---

## 4. 阶段 M2 — 密钥与部署（安全 P1）

**本期已结案**（工程口径）：**ZMK1/2/3** + **SQLCipher 域分离派生** + **ZMKP v1** + **JNI `initializeWithPassphrase`**；权威与运维见 **`03-Storage.md` 第4.2节 / 第4.3节**、**[03-M2-Minimal-ServerOwnedDevices.md](03-M2-Minimal-ServerOwnedDevices.md)**、**[04-M2-Key-Policy-And-Extensions.md](04-M2-Key-Policy-And-Extensions.md) 第6节**。**`05` 第7.3节** 已勾选。

**不在本期范围（未来单独立项）**：**HSM/TEE**、**libsecret/Credential Manager 替代 ZMK**、**ZMKP→ZMK 自动迁移**、**MM1 与 MM2 主密钥「大一统」**——见 **`04` 第3节**；立项后再做威胁模型与平台矩阵。

**若服务端管多设备**：以 **[03-M2-Minimal-ServerOwnedDevices.md](03-M2-Minimal-ServerOwnedDevices.md)** 为准（**设备权威仍在服务端**；本地 **`mm1_device_sessions`** 仅客户端缓存/恢复，**非**服务端真相源）。

**验收**：**最小闭环**＝ **`ZChatIM --test` 全绿** + **`03`/`04`/`05` 叙述一致**；**增强项**＝独立版本的 ADR + 测试矩阵（非当前默认门槛）。

---

## 5. 阶段 M3 — 协议/密码学大项（独立项目）

**结案（相对当前仓库）**：**M3 不是** `ZChatIM` C++ **本期交付义务**；**[05-ZChatIM-Implementation-Status.md](../02-Core/05-ZChatIM-Implementation-Status.md) 第7.4节** 勾选表示**文档与范围已结案**，**不**表示已实现双棘轮等愿景能力。权威表格式结案声明：**[01-MM1.md](../02-Core/01-MM1.md) 一点五.3**。

**权威拆分**：**[01-MM1.md](../02-Core/01-MM1.md) 一点五、一点五.2** 已将 **双棘轮、Random Allocator、纯内存消息块** 等与 **当前 MM2 持久化 IM** 对照列出；**默认客户端不实现**前者。

若产品立项 M3：**须重新定义** 双棘轮 / 内存模型 与 **MM2** 的关系（例如：仅 **E2EE 会话状态** 在 MM1，**落盘**仍为加密 blob；或 **「无本地历史」** 模式开关）。  

**不建议**在「补 C++ 窟窿」迭代里无边界扩张。

**验收**：独立设计文档 + 与服务器/客户端协议版本号 + **更新 `01-MM1.md` 一点五.2**「当前实现」列。

---

## 6. 建议执行顺序

1. 保持 **`--test`** 全绿前提下做 **M0**（**JNI 内存桩**已落地，余量为文档同步与小收尾）。  
2. **M1 产品级验收**（含重启的集成场景）：**当前因客户端 + Spring Boot 未就绪而阻塞**，见上文「集成联测」与 **`README`/`05` 第7.1节**。  
3. **M2** 与合规/上线窗口绑定；**服务端管多设备**时先跟 **[03-M2-Minimal-ServerOwnedDevices.md](03-M2-Minimal-ServerOwnedDevices.md)**（**文档对照即可先行**，不等待联测）。  
4. **M3** **独立立项**，不与 M0/M1 混排。

维护：完成某阶段后更新 **`05`** 对应行与本页勾选（可自行在副本中加 `[x]`）。
