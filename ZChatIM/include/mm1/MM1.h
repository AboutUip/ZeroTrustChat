#pragma once

#include "../Types.h"
#include "SecurityMemory.h"
#include "MemoryEncryption.h"
#include "SideChannel.h"
#include "AntiDebug.h"
#include "JniSecurity.h"
#include "KeyManagement.h"
#include "SecureRandom.h"
#include "managers/AuthSessionManager.h"
#include "managers/UserDataManager.h"
#include "managers/FriendManager.h"
#include "managers/GroupManager.h"
#include "managers/SystemControl.h"
#include "managers/SessionActivityManager.h"
#include "managers/DeviceSessionManager.h"
#include "managers/UserStatusManager.h"
#include "managers/GroupMuteManager.h"
#include "managers/MessageEditManager.h"
#include "managers/AccountDeleteManager.h"
#include "managers/CertPinningManager.h"
#include "managers/MessageEditOrchestration.h"
#include "managers/FriendVerificationManager.h"
#include "managers/GroupNameManager.h"
#include "managers/FriendNoteManager.h"
#include "managers/MentionPermissionManager.h"
#include "managers/MessageRecallManager.h"
#include "managers/MessageReplyManager.h"
#include "managers/LocalAccountCredentialManager.h"
#include "managers/RtcCallSessionManager.h"
#include <map>
#include <vector>
#include <string>
#include <cstdint>
#include <mutex>
#include "../common/JniSecurityPolicy.h"

namespace ZChatIM
{
    namespace mm1
    {
        // =============================================================
        // MM1 模块 - 安全内存框架
        // =============================================================
        //
        // **与 MM2 的分工**（实现与 [05-ZChatIM-Implementation-Status.md](../../../docs/02-Core/05-ZChatIM-Implementation-Status.md) 第3节 对齐）：
        // - **MM1**：JNI 信任边界上的校验、会话与策略；安全内存/RNG/密钥门面；各 **Manager** 验签后再路由到 MM2。
        // - **MM2**：编排、SQLite/ZDB 持久化；**`mm2::Crypto::Init` / `Cleanup` 的生命周期由 MM2 持有**（`MM1::Cleanup` 不调用 `Crypto::Cleanup`）。
        //
        // **mm1 → mm2 依赖边界**：允许 **`mm2::Crypto`**（及文档明确的 **Manager → `MM2::…`** 路由，如回复关系）；**禁止**在 MM1 中随意直调其它 MM2 存储 API 绕过 Manager 契约。
        //
        class MM1 {
            friend class MessageReplyManager;
            friend class MessageRecallManager;
            friend class FriendManager;
            friend class GroupManager;
            friend class GroupNameManager;
            friend class GroupMuteManager;
            friend class AccountDeleteManager;
            friend class FriendNoteManager;
            friend class MentionPermissionManager;
            friend class MessageEditManager;

        public:
            // =============================================================
            // 构造函数/析构函数
            // =============================================================
            
            MM1();
            ~MM1();
            
            // =============================================================
            // 初始化
            // =============================================================
            
            // 初始化
            bool Initialize();
            
            // 清理
            void Cleanup();

            // 可信区紧急擦除：**MM2::CleanupAllData**（若已初始化）+ 认证/多设备/在线/Pin/IM 活跃/@ALL 限速等 **MM1 进程内态** + **ClearMasterKey** + **Cleanup**。**不**修改 **JniBridge::m_initialized**（须由 **JniBridge::EmergencyWipe** 或 **Cleanup** 同步）。
            void EmergencyTrustedZoneWipe();

            // 供 **SystemControl::GetStatus** 等：无敏感载荷的进程内快照。
            std::map<std::string, std::string> SystemControlStatusSnapshot();
            
            // =============================================================
            // 安全内存操作
            // =============================================================
            
            // 分配安全内存
            void* AllocateSecureMemory(size_t size);
            
            // 释放安全内存
            void FreeSecureMemory(void* ptr);
            
            // 锁定内存（防止交换到磁盘）
            bool LockMemory(void* ptr, size_t size);
            
            // 解锁内存
            bool UnlockMemory(void* ptr, size_t size);
            
            // =============================================================
            // 内存加密
            // =============================================================
            
            // 加密内存
            bool EncryptMemory(void* ptr, size_t size, const uint8_t* key, size_t keySize);
            
            // 解密内存
            bool DecryptMemory(void* ptr, size_t size, const uint8_t* key, size_t keySize);
            
            // =============================================================
            // 侧信道防护
            // =============================================================
            
            // 常量时间比较
            bool ConstantTimeCompare(const void* a, const void* b, size_t size);
            
            // 安全内存清零
            void SecureZeroMemory(void* ptr, size_t size);
            
            // =============================================================
            // 反调试
            // =============================================================
            
            // 检测调试器
            bool IsDebuggerPresent();
            
            // 反调试保护
            bool EnableAntiDebug();
            
            // 禁用反调试保护
            void DisableAntiDebug();
            
            // =============================================================
            // JNI 安全
            // =============================================================
            
            // 验证 JNI 调用
            bool ValidateJniCall(const void* jniEnv, const void* jclass);
            
            // 安全的 JNI 字符串转换
            std::string JniStringToString(const void* jniEnv, const void* jstring);
            
            // 安全的 JNI 字节数组转换
            std::vector<uint8_t> JniByteArrayToVector(const void* jniEnv, const void* jbyteArray);
            
            // =============================================================
            // 密钥管理
            // =============================================================
            
            // 生成主密钥并写入 KeyManagement 进程内槽（与 HasMasterKey / JNI getStatus 一致）
            std::vector<uint8_t> GenerateMasterKey();

            // 以下在 MM1 递归锁内访问 KeyManagement，供 JNI 等避免「GetKeyManagement() 返回后失锁」竞态
            bool                HasMasterKey();
            void                ClearMasterKey();
            std::vector<uint8_t> RefreshMasterKey();
            
            // 生成/刷新会话密钥（同上，须经本类方法；勿 GetKeyManagement() 返回后再调）
            std::vector<uint8_t> GenerateSessionKey();
            std::vector<uint8_t> RefreshSessionKey();
            
            // 派生密钥
            std::vector<uint8_t> DeriveKey(const std::vector<uint8_t>& inputKey, const std::vector<uint8_t>& salt);
            
            // =============================================================
            // 安全随机数
            // =============================================================
            
            // 生成加密安全的随机字节
            std::vector<uint8_t> GenerateSecureRandom(size_t size);
            
            // =============================================================
            // 静态方法
            // =============================================================
            
            // 获取单例实例
            static MM1& Instance();
            
            // 获取子模块
            security::SecurityMemory& GetSecurityMemory();
            security::MemoryEncryption& GetMemoryEncryption();
            security::SideChannel& GetSideChannel();
            security::AntiDebug& GetAntiDebug();
            security::JniSecurity& GetJniSecurity();
            security::KeyManagement& GetKeyManagement();
            security::SecureRandom& GetSecureRandom();

            // =============================================================
            // 上层管理器契约（用于对齐文档闭环）
            // =============================================================
            AuthSessionManager& GetAuthSessionManager();
            // 在 MM1 锁内清空 AuthSessionManager 会话表及限流/封禁（供 JniBridge::EmergencyWipe 等）
            void ClearAllAuthSessions();

            UserDataManager& GetUserDataManager();
            FriendManager& GetFriendManager();
            GroupManager& GetGroupManager();
            SystemControl& GetSystemControl();

            SessionActivityManager& GetSessionActivityManager();
            DeviceSessionManager& GetDeviceSessionManager();
            UserStatusManager& GetUserStatusManager();

            GroupMuteManager& GetGroupMuteManager();
            MessageEditManager& GetMessageEditManager();
            AccountDeleteManager& GetAccountDeleteManager();
            CertPinningManager& GetCertPinningManager();

            MessageEditOrchestration& GetMessageEditOrchestration();

            FriendVerificationManager& GetFriendVerificationManager();
            GroupNameManager& GetGroupNameManager();
            FriendNoteManager& GetFriendNoteManager();
            MentionPermissionManager& GetMentionPermissionManager();
            MessageRecallManager& GetMessageRecallManager();
            MessageReplyManager& GetMessageReplyManager();

            LocalAccountCredentialManager& GetLocalAccountCredentialManager();
            RtcCallSessionManager& GetRtcCallSessionManager();
            
        private:
            // 禁止实例化
            MM1(const MM1&) = delete;
            MM1& operator=(const MM1&) = delete;
            
            // 成员变量
            bool m_initialized;
            // 最高安全性默认：所有 public 实例方法入口 SHOULD 持此递归锁（同线程可重入，避免 MM1 内部嵌套死锁）。
            mutable std::recursive_mutex m_apiRecursiveMutex;

            // =============================================================
            // 业务管理器成员（契约对象）
            // =============================================================
            AuthSessionManager m_authSessionManager;
            UserDataManager m_userDataManager;
            FriendManager m_friendManager;
            GroupManager m_groupManager;
            SystemControl m_systemControl;

            SessionActivityManager m_sessionActivityManager;
            DeviceSessionManager m_deviceSessionManager;
            UserStatusManager m_userStatusManager;

            GroupMuteManager m_groupMuteManager;
            MessageEditManager m_messageEditManager;
            AccountDeleteManager m_accountDeleteManager;
            CertPinningManager m_certPinningManager;
            MessageEditOrchestration m_messageEditOrchestration;
            FriendVerificationManager m_friendVerificationManager;
            GroupNameManager m_groupNameManager;
            FriendNoteManager m_friendNoteManager;
            MentionPermissionManager m_mentionPermissionManager;
            MessageRecallManager m_messageRecallManager;
            MessageReplyManager m_messageReplyManager;

            LocalAccountCredentialManager m_localAccountCredentialManager;
            RtcCallSessionManager m_rtcCallSessionManager;
            
            // 子模块
            security::SecurityMemory m_securityMemory;
            security::MemoryEncryption m_memoryEncryption;
            security::SideChannel m_sideChannel;
            security::AntiDebug m_antiDebug;
            security::JniSecurity m_jniSecurity;
            security::KeyManagement m_keyManagement;
            security::SecureRandom m_secureRandom;
        };
        
    } // namespace mm1
} // namespace ZChatIM
