#pragma once

// =============================================================================
// JNI / 可信 C++ 边界 — 安全策略（头文件级契约，供实现与审计遵循）
// =============================================================================
//
// 1) callerSessionId（首参）
//    - 除 Initialize / Cleanup / Auth / VerifySession / ValidateJniCall* 外，
//      JniInterface / JniBridge 的实例级业务 API **首参均为** callerSessionId。
//    - 会话有效性：JniBridge 使用 AuthSessionManager::TryGetSessionUserId(caller, outPrincipal)
//     （存在且未过期），与 VerifySession(caller) 对同一会话 id 的结论 **等价**；实现不强制调用
//      VerifySession 符号本身。
//    - principal 与后续参数的绑定 **按 API 分流**（与 src/jni/JniBridge.cpp 一致；权威表
//      docs/06-Appendix/01-JNI.md「principal 绑定矩阵」）：部分 API 仅要求 caller 有效；部分 API
//      另须 ConstantTimeCompare(principal, 某 16B userId/senderId/…)；StoreMessageReplyRelation
//      在 MessageReplyManager 内完成 TryGetSessionUserId + senderId 绑定。
//    - 群组：**`UpdateGroupName`**：`JniBridge` **`PrincipalMatches(principal, updaterId)`**（见 **01-JNI.md**）。
//      **InviteMember** / **RemoveMember** / **GetGroupMembers** / **UpdateGroupKey** 在 JniBridge **无**
//      与某 **16B userId** 的 **`PrincipalMatches`**，但 **GroupManager** 将 **principal** 作为操作者身份（见 **01-JNI.md** 矩阵下脚注）。
//    - **MM2::TryGetGroupKeyEnvelopeForMm1** / **SeedAcceptedFriendshipForSelfTest**：**不得**接入 JniBridge/Java；
//      前者泄露群密钥材料，后者可伪造 accepted 好友边（仅 `--test` 或受控 native 使用）。
//      InviteMember 另校验 **好友边**（**`friend_requests` accepted**）；UpdateGroupKey 轮换 **ZGK1**（**owner/admin**）。
//    - 长度约定：Types.h :: JNI_AUTH_SESSION_TOKEN_BYTES（默认 16，与 USER_ID_SIZE 一致）。
//
// 2) imSessionId（即时通讯会话）
//    - StoreMessage / GetSessionMessages / TouchSession 等第二参为 **聊天/会话通道 ID**，
//      与 ZSP 头内 4 字节 SESSION_ID_SIZE 无包含关系；二进制长度由实现与协议约定（常 16B）。
//
// 3) DestroySession
//    - callerSessionId：当前操作者会话；sessionIdToDestroy：待销毁目标。
//    - 实现 MUST：仅允许销毁与 principal 绑定的会话，或具备运维/管理员角色（由实现定义）。
//
// 4) 证书 / TLS 回调（VerifyPinnedServerCertificate / RecordFailure）
//    - 首参仍为 callerSessionId；**允许空 vector 当且仅当** 实现可证明调用源于受控 native TLS
//      回调且已做等价来源校验；否则 MUST 返回 false / 拒绝。
//
// 5) 并发（最高安全性默认）
//    - jni::JniBridge：实现 SHOULD 对每个 public 方法入口持 m_apiRecursiveMutex 全程排他锁，
//      再进入 MM1/MM2，避免跨线程状态撕裂（性能不足时再改为分层锁并重新审计）。
//    - mm1::MM1：**`MM1.cpp`** 已对每个 public 实例方法入口持 **`m_apiRecursiveMutex`**（**`Instance()`** 除外）；后续若改并发模型须重审计。
//    - mm2::MM2：实现 SHOULD 对每个 public 实例方法入口持 m_stateMutex。
//      GetMessageQueryManager()::List* 经 MM2 内部回调持同一 m_stateMutex，可与其它 MM2 API 交错调用。
//      GetStorageIntegrityManager() 返回引用不持锁；对其子调用仍须与 MM2 **串行** 或由 JniBridge 层互斥覆盖，
//      **不得**在无等价同步下跨线程持有/使用。
//
// 6) 初始化顺序（进程内集成）
//    - **`MM1::Initialize`** 会 **`mm2::Crypto::Init()`**（幂等）；**`MM2::Initialize(dataDir, indexDir)`** 可先于或后于 MM1，
//      须在首次依赖持久化/MM2 业务前成功。
//    - **`MM1::Cleanup`** **不**调用 **`Crypto::Cleanup`**；对称释放由 **`MM2::Cleanup`**（或等价编排）负责 **`Crypto::Cleanup`**。
//
// 7) 锁顺序（避免死锁）
//    - 需要同时触及 MM1 与 MM2 时：**先**取得 **`MM1::m_apiRecursiveMutex`**（或经 **`JniBridge::m_apiRecursiveMutex`** 已进入 MM1），**再**调用 MM2。
//    - **禁止**在已持有 **`MM2::m_stateMutex`** 的情况下回调 **`MM1`** 公开 API（除非未来引入明确的锁层级与证明无环）。
//
// =============================================================================

namespace ZChatIM {
namespace security_policy {
    // 仅作文档命名空间；无运行时代码。
} // namespace security_policy
} // namespace ZChatIM
