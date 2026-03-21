// MM1 skeleton implementation: satisfies linker / IntelliSense for declared API.
// Security and manager method bodies remain stubs until wired to real logic.

#include "mm1/MM1.h"

#include <cstring>

namespace ZChatIM::mm1 {

    MM1::MM1()
        : m_initialized(false)
    {
    }

    MM1::~MM1() = default;

    MM1& MM1::Instance()
    {
        static MM1 s_instance;
        return s_instance;
    }

    bool MM1::Initialize()
    {
        m_initialized = true;
        return true;
    }

    void MM1::Cleanup()
    {
        m_initialized = false;
    }

    void* MM1::AllocateSecureMemory(size_t size)
    {
        (void)size;
        return nullptr;
    }

    void MM1::FreeSecureMemory(void* ptr)
    {
        (void)ptr;
    }

    bool MM1::LockMemory(void* ptr, size_t size)
    {
        (void)ptr;
        (void)size;
        return false;
    }

    bool MM1::UnlockMemory(void* ptr, size_t size)
    {
        (void)ptr;
        (void)size;
        return false;
    }

    bool MM1::EncryptMemory(void* ptr, size_t size, const uint8_t* key, size_t keySize)
    {
        (void)ptr;
        (void)size;
        (void)key;
        (void)keySize;
        return false;
    }

    bool MM1::DecryptMemory(void* ptr, size_t size, const uint8_t* key, size_t keySize)
    {
        (void)ptr;
        (void)size;
        (void)key;
        (void)keySize;
        return false;
    }

    bool MM1::ConstantTimeCompare(const void* a, const void* b, size_t size)
    {
        if (!a || !b) {
            return false;
        }
        const auto* pa = static_cast<const unsigned char*>(a);
        const auto* pb = static_cast<const unsigned char*>(b);
        unsigned char diff = 0;
        for (size_t i = 0; i < size; ++i) {
            diff |= static_cast<unsigned char>(pa[i] ^ pb[i]);
        }
        return diff == 0;
    }

    void MM1::SecureZeroMemory(void* ptr, size_t size)
    {
        if (!ptr || size == 0) {
            return;
        }
        std::memset(ptr, 0, size);
    }

    bool MM1::IsDebuggerPresent()
    {
        return false;
    }

    bool MM1::EnableAntiDebug()
    {
        return false;
    }

    void MM1::DisableAntiDebug()
    {
    }

    bool MM1::ValidateJniCall(const void* jniEnv, const void* jclass)
    {
        (void)jniEnv;
        (void)jclass;
        return false;
    }

    std::string MM1::JniStringToString(const void* jniEnv, const void* jstring)
    {
        (void)jniEnv;
        (void)jstring;
        return {};
    }

    std::vector<uint8_t> MM1::JniByteArrayToVector(const void* jniEnv, const void* jbyteArray)
    {
        (void)jniEnv;
        (void)jbyteArray;
        return {};
    }

    std::vector<uint8_t> MM1::GenerateMasterKey()
    {
        return {};
    }

    std::vector<uint8_t> MM1::GenerateSessionKey()
    {
        return {};
    }

    std::vector<uint8_t> MM1::DeriveKey(
        const std::vector<uint8_t>& inputKey,
        const std::vector<uint8_t>& salt)
    {
        (void)inputKey;
        (void)salt;
        return {};
    }

    std::vector<uint8_t> MM1::GenerateSecureRandom(size_t size)
    {
        (void)size;
        return {};
    }

    security::SecurityMemory& MM1::GetSecurityMemory()
    {
        return m_securityMemory;
    }

    security::MemoryEncryption& MM1::GetMemoryEncryption()
    {
        return m_memoryEncryption;
    }

    security::SideChannel& MM1::GetSideChannel()
    {
        return m_sideChannel;
    }

    security::AntiDebug& MM1::GetAntiDebug()
    {
        return m_antiDebug;
    }

    security::JniSecurity& MM1::GetJniSecurity()
    {
        return m_jniSecurity;
    }

    security::KeyManagement& MM1::GetKeyManagement()
    {
        return m_keyManagement;
    }

    security::SecureRandom& MM1::GetSecureRandom()
    {
        return m_secureRandom;
    }

    AuthSessionManager& MM1::GetAuthSessionManager()
    {
        return m_authSessionManager;
    }

    UserDataManager& MM1::GetUserDataManager()
    {
        return m_userDataManager;
    }

    FriendManager& MM1::GetFriendManager()
    {
        return m_friendManager;
    }

    GroupManager& MM1::GetGroupManager()
    {
        return m_groupManager;
    }

    SystemControl& MM1::GetSystemControl()
    {
        return m_systemControl;
    }

    SessionActivityManager& MM1::GetSessionActivityManager()
    {
        return m_sessionActivityManager;
    }

    DeviceSessionManager& MM1::GetDeviceSessionManager()
    {
        return m_deviceSessionManager;
    }

    UserStatusManager& MM1::GetUserStatusManager()
    {
        return m_userStatusManager;
    }

    GroupMuteManager& MM1::GetGroupMuteManager()
    {
        return m_groupMuteManager;
    }

    MessageEditManager& MM1::GetMessageEditManager()
    {
        return m_messageEditManager;
    }

    AccountDeleteManager& MM1::GetAccountDeleteManager()
    {
        return m_accountDeleteManager;
    }

    CertPinningManager& MM1::GetCertPinningManager()
    {
        return m_certPinningManager;
    }

    MessageEditOrchestration& MM1::GetMessageEditOrchestration()
    {
        return m_messageEditOrchestration;
    }

    FriendVerificationManager& MM1::GetFriendVerificationManager()
    {
        return m_friendVerificationManager;
    }

    GroupNameManager& MM1::GetGroupNameManager()
    {
        return m_groupNameManager;
    }

    FriendNoteManager& MM1::GetFriendNoteManager()
    {
        return m_friendNoteManager;
    }

    MentionPermissionManager& MM1::GetMentionPermissionManager()
    {
        return m_mentionPermissionManager;
    }

    MessageRecallManager& MM1::GetMessageRecallManager()
    {
        return m_messageRecallManager;
    }

    MessageReplyManager& MM1::GetMessageReplyManager()
    {
        return m_messageReplyManager;
    }

} // namespace ZChatIM::mm1
