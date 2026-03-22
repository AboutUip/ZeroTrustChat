#pragma once

// JNI / native boundary — rules for JniBridge, MM1, MM2 (full: docs/06-Appendix/01-JNI.md).
//
// 1) callerSessionId first on business APIs except: Initialize, InitializeWithPassphrase, Cleanup, Auth,
//    VerifySession, RegisterLocalUser, AuthWithLocalPassword, HasLocalPassword,
//    ResetLocalPasswordWithRecovery, ValidateJniCall*. Binding: TryGetSessionUserId + 01-JNI principal matrix.
//    Do not JNI-expose: MM2::TryGetGroupKeyEnvelopeForMm1, SeedAcceptedFriendshipForSelfTest.
// 2) imSessionId: IM channel id (typically 16B), not ZSP 4-byte session field.
// 3) DestroySession: both session tokens resolve to same principal (current JniBridge).
// 4) Pin/TLS callbacks: optional transport over ZSP; empty caller only if impl proves equivalent origin.
// 5) Concurrency: JniBridge holds m_apiRecursiveMutex per entry; m_initialized atomic acquire/release;
//    MM1 public methods hold m_apiRecursiveMutex; MM2 holds m_stateMutex; MessageQueryManager only under MM2 lock;
//    GetStorageIntegrityManager() not locked — serialize callers.
// 6) Init: MM1::Initialize -> Crypto::Init; MM2::Initialize before first persistence use; MM1::Cleanup skips Crypto::Cleanup.
// 7) Lock order: MM1 (or JniBridge) before MM2; do not call MM1 from under MM2::m_stateMutex.
// 8) Wipe: NotifyExternalTrustedZoneWipeHandled() sole m_initialized=false (release). Full call graph: MM1.cpp, JniBridge.cpp.

namespace ZChatIM {
	namespace security_policy {
	} // namespace security_policy
} // namespace ZChatIM
