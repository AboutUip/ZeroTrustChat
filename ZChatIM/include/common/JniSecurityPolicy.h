#pragma once

// =============================================================================
// JNI / 可信 C++ 边界 — 安全策略（头文件级契约，供实现与审计遵循）
// =============================================================================
//
// 1) callerSessionId（首参）
//    - 除 Initialize / Cleanup / Auth / VerifySession / ValidateJniCall* 外，
//      JniInterface / JniBridge 的实例级业务 API **首参均为** callerSessionId。
//    - 实现 MUST：VerifySession(callerSessionId)==true 且未过期后，解析 principal userId，
//      再对后续参数做 **绑定校验**（如 userId==principal、群成员、文件属主等）。
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
//    - mm1::MM1：实现 SHOULD 对每个 public 实例方法入口持 m_apiRecursiveMutex。
//    - mm2::MM2：实现 SHOULD 对每个 public 实例方法入口持 m_stateMutex；调用
//      GetMessageQueryManager() 返回的查询接口时 **不得**在无 MM2 同步下泄露引用到其它线程。
//
// =============================================================================

namespace ZChatIM {
namespace security_policy {
    // 仅作文档命名空间；无运行时代码。
} // namespace security_policy
} // namespace ZChatIM
