// MM1: security facade delegates to `common::Memory` / security submodules; RNG & PBKDF2 via `mm2::Crypto`.
// Public instance methods hold `m_apiRecursiveMutex` per `JniSecurityPolicy.h`.

#include "mm1/MM1.h"

#include "common/Memory.h"
#include "mm2/storage/Crypto.h"

#include <cstring>
#include <mutex>

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
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        // Idempotent with MM2::Initialize / other callers; required for PBKDF2-backed DeriveKey paths.
        if (!mm2::Crypto::Init()) {
            return false;
        }
        m_initialized = true;
        return true;
    }

    void MM1::Cleanup()
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        // Do not call mm2::Crypto::Cleanup here — MM2 owns global crypto lifetime.
        m_initialized = false;
    }

    void* MM1::AllocateSecureMemory(size_t size)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        return m_securityMemory.Allocate(size);
    }

    void MM1::FreeSecureMemory(void* ptr)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        m_securityMemory.Free(ptr);
    }

    bool MM1::LockMemory(void* ptr, size_t size)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        return m_securityMemory.Lock(ptr, size);
    }

    bool MM1::UnlockMemory(void* ptr, size_t size)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        return m_securityMemory.Unlock(ptr, size);
    }

    bool MM1::EncryptMemory(void* ptr, size_t size, const uint8_t* key, size_t keySize)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!ptr || size == 0 || !key || keySize == 0) {
            return false;
        }
        common::Memory::EncryptMemory(ptr, size, key, keySize);
        return true;
    }

    bool MM1::DecryptMemory(void* ptr, size_t size, const uint8_t* key, size_t keySize)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        if (!ptr || size == 0 || !key || keySize == 0) {
            return false;
        }
        common::Memory::DecryptMemory(ptr, size, key, keySize);
        return true;
    }

    bool MM1::ConstantTimeCompare(const void* a, const void* b, size_t size)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        return common::Memory::ConstantTimeCompare(a, b, size);
    }

    void MM1::SecureZeroMemory(void* ptr, size_t size)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        common::Memory::SecureZero(ptr, size);
    }

    bool MM1::IsDebuggerPresent()
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        return m_antiDebug.IsDebuggerPresent();
    }

    bool MM1::EnableAntiDebug()
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        return m_antiDebug.Enable();
    }

    void MM1::DisableAntiDebug()
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
    }

    bool MM1::ValidateJniCall(const void* jniEnv, const void* jclass)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        return m_jniSecurity.ValidateCall(jniEnv, jclass);
    }

    std::string MM1::JniStringToString(const void* jniEnv, const void* jstring)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        return m_jniSecurity.StringFromJni(jniEnv, jstring);
    }

    std::vector<uint8_t> MM1::JniByteArrayToVector(const void* jniEnv, const void* jbyteArray)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        return m_jniSecurity.ByteArrayFromJni(jniEnv, jbyteArray);
    }

    std::vector<uint8_t> MM1::GenerateMasterKey()
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        return m_keyManagement.GenerateMasterKey();
    }

    std::vector<uint8_t> MM1::GenerateSessionKey()
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        return m_keyManagement.GenerateSessionKey();
    }

    std::vector<uint8_t> MM1::DeriveKey(
        const std::vector<uint8_t>& inputKey,
        const std::vector<uint8_t>& salt)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        return m_keyManagement.DeriveKey(inputKey, salt);
    }

    std::vector<uint8_t> MM1::GenerateSecureRandom(size_t size)
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        return m_secureRandom.Generate(size);
    }

    security::SecurityMemory& MM1::GetSecurityMemory()
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        return m_securityMemory;
    }

    security::MemoryEncryption& MM1::GetMemoryEncryption()
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        return m_memoryEncryption;
    }

    security::SideChannel& MM1::GetSideChannel()
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        return m_sideChannel;
    }

    security::AntiDebug& MM1::GetAntiDebug()
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        return m_antiDebug;
    }

    security::JniSecurity& MM1::GetJniSecurity()
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        return m_jniSecurity;
    }

    security::KeyManagement& MM1::GetKeyManagement()
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        return m_keyManagement;
    }

    security::SecureRandom& MM1::GetSecureRandom()
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        return m_secureRandom;
    }

    AuthSessionManager& MM1::GetAuthSessionManager()
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        return m_authSessionManager;
    }

    UserDataManager& MM1::GetUserDataManager()
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        return m_userDataManager;
    }

    FriendManager& MM1::GetFriendManager()
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        return m_friendManager;
    }

    GroupManager& MM1::GetGroupManager()
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        return m_groupManager;
    }

    SystemControl& MM1::GetSystemControl()
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        return m_systemControl;
    }

    SessionActivityManager& MM1::GetSessionActivityManager()
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        return m_sessionActivityManager;
    }

    DeviceSessionManager& MM1::GetDeviceSessionManager()
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        return m_deviceSessionManager;
    }

    UserStatusManager& MM1::GetUserStatusManager()
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        return m_userStatusManager;
    }

    GroupMuteManager& MM1::GetGroupMuteManager()
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        return m_groupMuteManager;
    }

    MessageEditManager& MM1::GetMessageEditManager()
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        return m_messageEditManager;
    }

    AccountDeleteManager& MM1::GetAccountDeleteManager()
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        return m_accountDeleteManager;
    }

    CertPinningManager& MM1::GetCertPinningManager()
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        return m_certPinningManager;
    }

    MessageEditOrchestration& MM1::GetMessageEditOrchestration()
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        return m_messageEditOrchestration;
    }

    FriendVerificationManager& MM1::GetFriendVerificationManager()
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        return m_friendVerificationManager;
    }

    GroupNameManager& MM1::GetGroupNameManager()
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        return m_groupNameManager;
    }

    FriendNoteManager& MM1::GetFriendNoteManager()
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        return m_friendNoteManager;
    }

    MentionPermissionManager& MM1::GetMentionPermissionManager()
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        return m_mentionPermissionManager;
    }

    MessageRecallManager& MM1::GetMessageRecallManager()
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        return m_messageRecallManager;
    }

    MessageReplyManager& MM1::GetMessageReplyManager()
    {
        const std::lock_guard<std::recursive_mutex> lk(m_apiRecursiveMutex);
        return m_messageReplyManager;
    }

} // namespace ZChatIM::mm1
