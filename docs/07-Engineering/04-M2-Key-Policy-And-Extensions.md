# MM2 密钥策略与扩展（M2 Key Policy）

**目的**：把 **`mm2_message_key.bin`**（消息主密钥落盘形态）与 **SQLCipher 元库密钥** 的职责写清，并列出 **已落地 / 集成点 / 明确不做** 项，便于产品与合规对齐。

**权威实现**：**`ZChatIM/include/mm2/MM2.h`**、**`MM2.cpp`**、**`MM2_message_key_passphrase.cpp`**（**ZMKP**）、**`MM2_message_key_darwin.cpp`**（**ZMK3**）、**`03-Storage.md` 第4.2节**（SQLCipher 域分离派生）。

---

## 1. 职责划分

| 材料 | 作用 |
|------|------|
| **32B 消息主密钥**（内存 **`m_messageStorageKey`**） | **`.zdb` 上文件分片等** AES-GCM、**IM RAM** 消息加解密、以及（默认构建下）**SQLCipher 元库 raw key 的派生根**（**非**直接当 SQLCipher PRAGMA key）。 |
| **SQLCipher 密钥** | 由固定域串 **`DeriveMetadataSqlcipherKeyFromMessageMaster`**（**SHA-256**）从上述主密钥派生，与消息用途 **域分离**。 |
| **ZMK1 / ZMK2 / ZMK3** | 用 **DPAPI / 机器+用户+indexDir / Keychain** 等方式保护盘上 **32B 主密钥**（或等价封装），与 **用户口令** 无关。 |
| **ZMKP v1**（**`ZMKP` + 0x01**） | **用户口令 → PBKDF2-HMAC-SHA256（文件内记录迭代次数，默认 200000）→ AES-256-GCM** 包裹 **32B 主密钥**；**不**替代 SQLCipher 派生链。 |

**约束**：**口令从不直接**作为 SQLCipher 的 raw key；**ZMKP** 只解决「盘上主密钥 blob 的机密性」这一层。

---

## 2. API 与集成点

- **C++**：**`MM2::Initialize(dataDir, indexDir, messageKeyPassphraseUtf8)`** — 非空指针且非空 C 串时创建/加载 **ZMKP**（新库写 ZMKP；已有 ZMKP 须匹配口令）；**已有 ZMK1/2/3** 时 **不得** 再带口令（否则失败）。**`ZCHATIM_USE_SQLCIPHER=OFF`** 时 **禁止** 非空口令（与元库路径一致）。
- **JNI**：**`initializeWithPassphrase`** → **`JniInterface::InitializeWithPassphrase`** → **`MM2::Initialize(..., passphrase)`**（见 **`01-JNI.md`**）。
- **MM1**：会话密钥、**Ed25519**、**ZGK1** 群信封等 **不**与本文件主密钥混用；若产品要「统一密钥策略」，须在 **协议/备份** 层另表，**不**在本文件改 SQLCipher 链。

---

## 3. 明确不做（当前仓库默认）

以下项 **未实现** 或 **非默认路径**；若合规要求，须 **单独立项** 并更新本文与 **`05-ZChatIM-Implementation-Status.md`**：

- **HSM**、**TEE** 托管 **mm2** 主密钥或 SQLCipher key。
- **libsecret** / **Credential Manager** 等系统密钥环 **替代** ZMK1/2/3（可作为未来 **可选存储后端**，与 ZMKP 正交）。
- **ZMKP → ZMK1/2/3 自动迁移**（产品若需要须定义「解锁后重写」流程与失败回滚）。
- **01-MM1.md** 中的 **双棘轮、Random Allocator 式纯内存 IM**（**M3** 愿景）：见 **`01-MM1.md` 一点五**、**`02-Cpp-Completion-Roadmap.md` 第5节**。

---

## 4. 运维与恢复（摘要）

- 丢失 **口令** 且使用 **ZMKP**：盘上 blob **不可恢复**（设计如此）；路径＝**删 `indexDir` 关键文件 / `CleanupAllData`** 后重装与同步。
- 丢失 **ZMK1/2/3** 环境（换机、删 DPAPI/Keychain）：同 **03-Storage.md 第4.3节**。

---

## 5. 相关文档

- **`docs/02-Core/03-Storage.md`** — 密钥表与 SQLCipher 参数。
- **`docs/07-Engineering/03-M2-Minimal-ServerOwnedDevices.md`** — M2 最小落地与测试清单。
- **`docs/02-Core/05-ZChatIM-Implementation-Status.md`** — P1 / P2 勾选。

---

## 6. 本期工程结案（相对当前仓库）

**已纳入本期交付边界的 M2 能力**（与 **`05` 第7.3节**、**`03` 第5节**一致）：

- **ZMK1 / ZMK2 / ZMK3** 保护 **`mm2_message_key.bin`**（**Win DPAPI** / **非 Apple Unix 派生+GCM** / **Apple Keychain**）。
- **ZMKP v1**：口令 → PBKDF2 → AES-GCM 包裹 **32B 主密钥**；**`MM2::Initialize(..., passphrase)`** 与 **JNI `initializeWithPassphrase`**（见 **`01-JNI.md`**）。
- **SQLCipher 元库密钥**：**域分离**自消息主密钥（**`03-Storage.md` 第4.2节`**），**不**因 ZMKP 而改变该链。

**上述交付边界之外的「增强」——本期明确不实现**（仍见 **第3节「明确不做」**）：**HSM / TEE**、**libsecret / Credential Manager 替代 ZMK1/2/3**、**ZMKP→ZMK 自动迁移**、**MM1 与 MM2 主密钥叙事大一统（协议/备份层另表）**。若合规强制要求：**须单独立项**（设计 ADR + 平台矩阵 + 更新本文第3节与 **`05`**），**不**占用当前 **`ZChatIM --test`** 默认发版门槛。

**与服务端管多设备的关系**：以 **`03-M2-Minimal-ServerOwnedDevices.md`** 为前提时，**设备会话 / IM 活跃 / 证书 Pin 客户端态** 可落 **SQLCipher 元库**（**`mm1_*` 表**），与 **`mm2_message_key.bin` / ZMK** 形态**解耦**（密钥文件保护主密钥，**不**替代服务端设备权威）；**HSM/密钥环** 仍属上段「增强」范畴。
