# M2 最小实现清单（前提：服务端管多设备）

**前提（你已确认）**：**设备列表 / 踢设备 / 登录态** 以**服务端为权威**；客户端 **`DeviceSessionManager` / `SessionActivityManager` / `CertPinningManager`** 可经 **`mm1_*` 元表**做**本地重启恢复**（**`user_version=11`**），**不**改变「服务端为权威」；**`UserStatusManager`** 经 **`mm1_user_status`** 存**最后已知**在线显示，**仍不**替代服务端在线真相。

**目标**：在不大改业务面的前提下，把 **本地数据根密钥（`mm2_message_key.bin`）+ SQLCipher 派生链** 做到**可部署、可说明、可测试**。

**文档验收（可先于联测）**：**不依赖** Android / Spring Boot 端到端环境；发布或对接前，将本文 **§1 表**、**服务端设备权威**前提与 **`docs/02-Core/03-Storage.md` 第4.2 / 4.3节** 写入运维/发布说明即可。联测阻塞见 **`docs/README.md` 第2节**、**`05-ZChatIM-Implementation-Status.md` 第7.1节**。

---

## 1. 与代码现状对齐（先读再改）

| 能力 | 实现落点 | 说明 |
|------|-----------|------|
| 消息主密钥文件 | **`ZChatIM/src/mm2/MM2.cpp`** | **`indexDir/mm2_message_key.bin`**：**ZMK1**（Win **DPAPI**）、**ZMK2**（非 Apple Unix **机器/用户/indexDir 派生 + GCM**）、**ZMK3**（Apple **Keychain 封装**）；兼容旧版 **32B 明文**并**尽力迁移**。 |
| Apple 密钥链 | **`ZChatIM/src/mm2/MM2_message_key_darwin.cpp`** | **ZMK3** 封装密钥 **Keychain** service 名见源码常量。 |
| 元数据库密钥 | **`SqliteMetadataDb::DeriveMetadataSqlcipherKeyFromMessageMaster`**（**`SqliteMetadataDb.cpp`**） | 与 **`.zdb` AES-GCM** **域分离**派生（**`03-Storage.md` 第4.2节**）。 |
| 对称密码原语 | **`ZChatIM/src/mm2/storage/Crypto.cpp`** | **OpenSSL 3**；**Windows** 链 **`crypt32`**（DPAPI）。 |

**结论**：**ZMK1/2/3 + SQLCipher 派生**已在 **`MM2.cpp`** 等落地；**步 A/B/C 已收尾**。**步 D（口令 / ZMKP v1）**已在 C++ 落地（**`MM2_message_key_passphrase.cpp`**、**`MM2::Initialize(..., passphrase)`**）；**`--test`** 含 **`ZMKP`** 子用例（**`ZCHATIM_USE_SQLCIPHER=ON`** 时编译进 **`RunMm2MessageKeyProtectedFileTests`**）。

---

## 2. 建议实施顺序（最小闭环）

### 步 A — 文档与注释扫尾（0.5～1 天）

- [x] 对照 **`MM2.cpp`** 更新 **`docs/02-Core/03-Storage.md` 第七节** **`MM2` 行**中 **`mm2_message_key.bin`** 表述（**ZMK1 / ZMK2 / ZMK3** + 旧 **32B 明文**迁移）；并与 **第三节** 密钥表交叉引用；仓库 **`docs/`** 与关键 **`ZChatIM/**` 注释已统一为 **「第×节 / 第×条」**（与 **`docs/README.md` 维护约定**：正文勿用西文章节号代替）。
- [x] 对照更新 **`include/mm2/MM2.h`** **`Initialize` 注释**中「非 Windows 仅明文」的过时句。
- [x] **`docs/07-Engineering/02-Cpp-Completion-Roadmap.md`** 的 **M2** 已指向本文。

**验收**：**`05`**、**`03-Storage` 第七节**、**`MM2.h`** 三处对 **ZMK1/2/3** 的描述无互斥。

### 步 B — 运维与产品说明（并行文案）

- [x] 已写入 **`docs/02-Core/03-Storage.md` 第4.3节**（运维与密钥恢复）：含 **丢密钥/库**、**换机**、**多用户同目录**、**`Cleanup` vs `CleanupAllData`**（**删 `mm2_message_key.bin` / Keychain**）及 **紧急擦除**与 **`JniSecurityPolicy.h` 第8节**的交叉引用。发布说明可摘录该节要点。

**验收**：客服/集成方问题「删了密钥会怎样」「换电脑能拷文件夹吗」「退出和销毁区别」有**单一权威段落**可答。

### 步 C — 测试矩阵

- [x] **自动化（`ZChatIM --test`）**  
  - **`RunMm2MessageKeyProtectedFileTests`**（**`mm1_managers_test.cpp`**）：新库 **`mm2_message_key.bin`** 为封装格式（**>32B**，**ZMK1/2/3** 之一），**二次 `Initialize`** 可读；**SQLCipher 构建**下另测 **ZMKP**（魔数、同口令重开、错口令失败、无口令失败、已有 **ZMK1/2/3** 时带口令失败）。  
  - **`CleanupAllData`** 删除 **`.zdb` / 元库 / 密钥文件**：见 **`mm1_managers_test.cpp`** 与 **`mm2_fifty_scenarios_test.cpp`**（如 **S48**）。  
  - **Windows 专用**：**`MM2 legacy plain mm2_message_key migrates to ZMK1 (DPAPI)`**（**`#ifdef _WIN32`**）。  
- [ ] **发布前手工（建议）**：**macOS/iOS** 上 **ZMK3** + Keychain 权限/错误文案；**Linux** 篡改密钥文件后的 **`LastError`**（可选，非阻塞合并）。

| 平台 | 仍建议手工扫一眼 |
|------|------------------|
| Windows | 与 CI 一致时可跳过 |
| Linux | 篡改 **`mm2_message_key.bin`** 后 **`Initialize`** 失败是否可理解 |
| macOS / iOS | **ZMK3**、Keychain 拒绝时的体验 |

### 步 D — 用户口令（第二保护层，ZMKP v1）

**已实现（C++）**：**不**把口令直接当 SQLCipher key；保持 **域分离 SHA-256** 链；**口令 → PBKDF2 → AES-GCM** 包裹 **32B 消息主密钥**（**`MM2_message_key_passphrase.cpp`**）。**`MM2::Initialize(dataDir, indexDir, passphrase)`** 与 **`LoadOrCreateMessageStorageKeyUnlocked`** 行为见 **`MM2.h`**、**`04-M2-Key-Policy-And-Extensions.md`**。

**JNI**：由 **`JniBridge` / `JniNatives`** 等传入口令（与 **`MM2::Initialize(..., passphrase)`** 对齐）；Java 侧接线以 **`01-JNI.md`** 为准。

**验收**：口令错误 **恒定失败**；**Forgot** 路径＝**删 `indexDir` 关键文件 / `CleanupAllData`** 后重装（见 **`03-Storage.md` 第4.3节**）。

---

## 3. 明确不做（在本前提下）

- **`DeviceSessionManager` 等**已落 **`mm1_device_sessions`** 等（**M1**，须 **`MM2::Initialize`**）；**服务端仍为设备权威**，本地表用于**重启恢复**与 **≤2 设备**客户端策略，**不**替代服务端踢设备/登录态真相。
- **不上 HSM / libsecret**，除非合规单独立项。
- **不把 `01-MM1.md` 双棘轮** 混进本清单（属 **M3**）。

---

## 4. 涉及文件速查（改代码时）

- **`ZChatIM/src/mm2/MM2.cpp`** — 主密钥加载/创建/迁移。
- **`ZChatIM/src/mm2/MM2_message_key_passphrase.cpp`** — **ZMKP v1**（口令 + PBKDF2 + GCM 包裹 32B 主密钥）。
- **`ZChatIM/src/mm2/MM2_message_key_darwin.cpp`** — **ZMK3**。
- **`ZChatIM/src/mm2/storage/SqliteMetadataDb.cpp`** — **`DeriveMetadataSqlcipherKeyFromMessageMaster`**、**`Open(..., key)`**。
- **`ZChatIM/include/mm2/MM2.h`** — 对外语义注释。
- **`docs/02-Core/03-Storage.md`** — 第4.2节、第七节表。
- **`docs/02-Core/05-ZChatIM-Implementation-Status.md`** — 第7.3节勾选与「仍待」缩窄表述。

---

## 5. 收尾状态（M2 最小闭环）

| 项 | 状态 |
|----|------|
| **步 A** 文档 / **`MM2.h`** | 已完成 |
| **步 B** **`03-Storage.md` 第4.3节** | 已完成 |
| **步 C** **`--test` + 清单上列出的用例** | 已完成（**Darwin 细测**见上表 **可选**） |
| **步 D** 用户口令层（**ZMKP**） | **C++ + JNI 已落地**；**`--test`** 覆盖（**`SQLCipher` ON**） |
| **M2「增强」**（**`04` 第3节 / 第6节**） | **本期文档结案**：**HSM / libsecret / MM1 密钥大一统** 等**不**在本仓库本期实现；合规需要时 **单独立项** |

**后续（代码）**：仅当产品立项 **HSM / 系统密钥环 / 统一密钥策略** 时开新版本，并同步 **`04` / `05` / `01-JNI.md`**。**发布前手工**（上表 **步 C**）仍建议按平台扫 **ZMK3 / 篡改密钥 `LastError`**，属**集成清单**非源码阻塞项。
