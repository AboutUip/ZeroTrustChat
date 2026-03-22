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
#include "managers/VoiceVideoCallManager.h"
#include "managers/RtcCallManager.h"
#include "managers/MediaCallCoordinator.h"
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
        // MM1: JNI trust boundary, managers, Crypto::Init only; MM2 owns Crypto::Cleanup. Manager->MM2 routes only as documented.
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
            MM1();
            ~MM1();

            bool Initialize();
            void Cleanup();

            // Full disk + MM1 state; does not set JniBridge::m_initialized (use JniBridge/SystemControl paths for that).
            void EmergencyTrustedZoneWipe();

            std::map<std::string, std::string> SystemControlStatusSnapshot();

            void* AllocateSecureMemory(size_t size);
            void FreeSecureMemory(void* ptr);
            bool LockMemory(void* ptr, size_t size);
            bool UnlockMemory(void* ptr, size_t size);

            bool EncryptMemory(void* ptr, size_t size, const uint8_t* key, size_t keySize);
            bool DecryptMemory(void* ptr, size_t size, const uint8_t* key, size_t keySize);

            bool ConstantTimeCompare(const void* a, const void* b, size_t size);
            void SecureZeroMemory(void* ptr, size_t size);

            bool IsDebuggerPresent();
            bool EnableAntiDebug();
            void DisableAntiDebug();

            bool ValidateJniCall(const void* jniEnv, const void* jcls);
            std::string JniStringToString(const void* jniEnv, const void* jstr);
            std::vector<uint8_t> JniByteArrayToVector(const void* jniEnv, const void* jbytes);

            std::vector<uint8_t> GenerateMasterKey();

            // Locked; do not call KeyManagement after GetKeyManagement() returns without holding MM1 lock.
            bool                HasMasterKey();
            void                ClearMasterKey();
            std::vector<uint8_t> RefreshMasterKey();

            std::vector<uint8_t> GenerateSessionKey();
            std::vector<uint8_t> RefreshSessionKey();

            std::vector<uint8_t> DeriveKey(const std::vector<uint8_t>& inputKey, const std::vector<uint8_t>& salt);

            std::vector<uint8_t> GenerateSecureRandom(size_t size);

            static MM1& Instance();

            security::SecurityMemory& GetSecurityMemory();
            security::MemoryEncryption& GetMemoryEncryption();
            security::SideChannel& GetSideChannel();
            security::AntiDebug& GetAntiDebug();
            security::JniSecurity& GetJniSecurity();
            security::KeyManagement& GetKeyManagement();
            security::SecureRandom& GetSecureRandom();

            // Get*Manager(): lock only covers pointer fetch; JNI serializes via JniBridge. See JniSecurityPolicy.h 5–7.
            AuthSessionManager& GetAuthSessionManager();
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
            VoiceVideoCallManager& GetVoiceVideoCallManager();
            RtcCallManager& GetRtcCallManager();
            MediaCallCoordinator& GetMediaCallCoordinator();

        private:
            MM1(const MM1&) = delete;
            MM1& operator=(const MM1&) = delete;

            bool m_initialized;
            mutable std::recursive_mutex m_apiRecursiveMutex;

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
            VoiceVideoCallManager m_voiceVideoCallManager;
            RtcCallManager m_rtcCallManager;
            MediaCallCoordinator m_mediaCallCoordinator;

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
