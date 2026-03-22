# 证书固定技术规范

> **文档类型**：**TLS 公钥固定（Pinning）** 与客户端封禁策略。  
> **JNI 契约**：**`docs/06-Appendix/01-JNI.md`**「十、安全模块」——`ConfigurePinnedPublicKeyHashes`、`VerifyPinnedServerCertificate`、`IsClientBanned`、`RecordFailure`、`ClearBan` 等。  
> **实现状态**：**`JniBridge` + JNI** 已路由 **`CertPinningManager`**（**`MM1_manager_stubs.cpp`** → **`MM2` → `mm1_cert_pin_config` / `mm1_cert_pin_client`**，**`user_version=11`**；**进程重启可恢复** SPKI 配置与按 **`client_id`** 的失败/封禁，须 **`MM2::Initialize`**）；**`caller` 可为空** 的路径仅当策略可证明等价来源（见 **`JniSecurityPolicy.h`**）。**TLS 握手提取 SPKI** 仍属集成方责任。

---

## 一、固定方式

```
┌─────────────────────────────────────────────────────────────┐
│  公钥哈希固定 (SPKI SHA-256)                                  │
├─────────────────────────────────────────────────────────────┤
│  当前证书公钥哈希                                              │
│  备用证书公钥哈希                                              │
└─────────────────────────────────────────────────────────────┘
```

---

## 二、验证流程

```
1. TLS握手获取服务器证书
2. 提取公钥 → SHA-256
3. 与内置哈希比对
4. 不匹配 → 断开连接 → 标记异常
5. 连续3次异常 → 封禁客户端
```

---

## 三、自动轮换

```
服务端:
├── Active Certificate (当前)
└── Standby Certificate (备用)

轮换: Active → Standby，新证书生效
```

客户端 MUST 同时信任 **当前 SPKI 哈希** 与 **备用 SPKI 哈希**（见 JNI `configurePinnedPublicKeyHashes` 参数语义）。

---

## 四、异常封禁

| 项目 | 值 |
|------|-----|
| 触发 | 连续3次证书验证失败 |
| 存储 | **MM1 内存**（目标）；与 **`RecordFailure` / `IsClientBanned`** 联动 |
| 解除 | 管理员手动（`clearBan`） |

---

## 五、客户端内置

- 当前公钥哈希
- 备用公钥哈希
- 算法: SHA-256

---

## 六、并发与安全不变量

- **`JniBridge`** 应在 **`m_apiRecursiveMutex`** 下进入 MM1（**`01-JNI.md`** 文首）。  
- **TLS 回调**中带 **`caller`**：可为空时的威胁模型见 **`JniSecurityPolicy.h`**，**不得**在文档中省略。

---

## 七、相关文档

| 文档 | 用途 |
|------|------|
| [01-JNI.md](../06-Appendix/01-JNI.md) | 方法签名与 caller 规则 |
| [ZChatIM/docs/JNI-API-Documentation.md](../../ZChatIM/docs/JNI-API-Documentation.md) | 路由与安全细则 |
| [05-ZChatIM-Implementation-Status.md](../02-Core/05-ZChatIM-Implementation-Status.md) | 实现进度 |
