// mm1::security::* — memory/RNG/key helpers delegate to `common::*` and `mm2::Crypto`.

#include "mm1/SecurityMemory.h"
#include "mm1/MemoryEncryption.h"
#include "mm1/SideChannel.h"
#include "mm1/AntiDebug.h"
#include "mm1/JniSecurity.h"
#include "mm1/KeyManagement.h"
#include "mm1/SecureRandom.h"

#include "common/Memory.h"
#include "common/Random.h"
#include "mm2/storage/Crypto.h"
#include "Types.h"

#include <cstring>
#include <mutex>
#include <random>
#include <string>

#if defined(_WIN32)
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>
#endif
#if defined(ZCHATIM_HAVE_JNI)
#    include <jni.h>
#endif

namespace ZChatIM::mm1::security {

    // --- SecurityMemory ---
    SecurityMemory::SecurityMemory()
        : m_totalAllocated(0)
        , m_peakUsage(0)
    {
    }

    SecurityMemory::~SecurityMemory() = default;

    void* SecurityMemory::Allocate(size_t size)
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        void* p = common::Memory::Allocate(size);
        if (p) {
            m_allocatedMemory[p] = size;
        }
        return p;
    }

    void SecurityMemory::Free(void* ptr)
    {
        if (!ptr) {
            return;
        }
        std::lock_guard<std::mutex> lk(m_mutex);
        m_allocatedMemory.erase(ptr);
        common::Memory::Free(ptr);
    }

    void* SecurityMemory::Reallocate(void* ptr, size_t newSize)
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        void* q = common::Memory::Reallocate(ptr, newSize);
        if (newSize == 0 && ptr) {
            m_allocatedMemory.erase(ptr);
            return nullptr;
        }
        if (!q) {
            return nullptr;
        }
        if (ptr && ptr != q) {
            m_allocatedMemory.erase(ptr);
        }
        m_allocatedMemory[q] = newSize;
        return q;
    }

    bool SecurityMemory::Lock(void* ptr, size_t size)
    {
        if (!ptr || size == 0) {
            return false;
        }
        return common::Memory::LockMemory(ptr, size);
    }

    bool SecurityMemory::Unlock(void* ptr, size_t size)
    {
        if (!ptr || size == 0) {
            return false;
        }
        return common::Memory::UnlockMemory(ptr, size);
    }

    bool SecurityMemory::Protect(void* ptr, size_t size, int protection)
    {
        if (!ptr || size == 0) {
            return false;
        }
        return common::Memory::ProtectMemory(ptr, size, protection);
    }

    bool SecurityMemory::IsAccessible(const void* ptr, size_t size)
    {
        if (!ptr || size == 0) {
            return false;
        }
        return common::Memory::IsMemoryAccessible(ptr, size);
    }

    bool SecurityMemory::IsLocked(const void* ptr, size_t size)
    {
        (void)ptr;
        (void)size;
        return false;
    }

    size_t SecurityMemory::GetAllocatedSize() const
    {
        return common::Memory::GetAllocatedSize();
    }

    size_t SecurityMemory::GetPeakMemoryUsage() const
    {
        return common::Memory::GetPeakMemoryUsage();
    }

    void SecurityMemory::ResetMemoryStats()
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        common::Memory::ResetMemoryStats();
    }

    // --- MemoryEncryption ---
    MemoryEncryption::MemoryEncryption()  = default;
    MemoryEncryption::~MemoryEncryption() = default;

    bool MemoryEncryption::Encrypt(void* ptr, size_t size, const uint8_t* key, size_t keySize)
    {
        if (!ptr || size == 0 || !key || keySize == 0) {
            return false;
        }
        return EncryptXor(ptr, size, key, keySize);
    }

    bool MemoryEncryption::Decrypt(void* ptr, size_t size, const uint8_t* key, size_t keySize)
    {
        if (!ptr || size == 0 || !key || keySize == 0) {
            return false;
        }
        return DecryptXor(ptr, size, key, keySize);
    }

    bool MemoryEncryption::EncryptBlock(void* ptr, size_t size, const uint8_t* key)
    {
        if (!ptr || size == 0 || !key) {
            return false;
        }
        common::Memory::EncryptMemory(ptr, size, key, CRYPTO_KEY_SIZE);
        return true;
    }

    bool MemoryEncryption::DecryptBlock(void* ptr, size_t size, const uint8_t* key)
    {
        if (!ptr || size == 0 || !key) {
            return false;
        }
        common::Memory::DecryptMemory(ptr, size, key, CRYPTO_KEY_SIZE);
        return true;
    }

    bool MemoryEncryption::GenerateKey(uint8_t* key, size_t keySize)
    {
        if (!key || keySize == 0) {
            return false;
        }
        const auto rnd = mm2::Crypto::GenerateSecureRandom(keySize);
        if (rnd.size() != keySize) {
            return false;
        }
        std::memcpy(key, rnd.data(), keySize);
        return true;
    }

    bool MemoryEncryption::DeriveKey(
        const uint8_t* inputKey,
        size_t         inputKeySize,
        uint8_t*       outputKey,
        size_t         outputKeySize)
    {
        if (!inputKey || inputKeySize == 0 || !outputKey || outputKeySize == 0) {
            return false;
        }
        static const char kCtx[] = "ZChatIM|MM1|MemoryEncryption|v1";
        if (!mm2::Crypto::Init()) {
            return false;
        }
        return mm2::Crypto::DeriveKey(
            inputKey,
            inputKeySize,
            reinterpret_cast<const uint8_t*>(kCtx),
            sizeof(kCtx) - 1U,
            outputKey,
            outputKeySize);
    }

    bool MemoryEncryption::IsValidKeySize(size_t keySize)
    {
        return keySize == CRYPTO_KEY_SIZE;
    }

    size_t MemoryEncryption::GetRecommendedKeySize()
    {
        return CRYPTO_KEY_SIZE;
    }

    bool MemoryEncryption::EncryptXor(void* ptr, size_t size, const uint8_t* key, size_t keySize)
    {
        if (!ptr || size == 0 || !key || keySize == 0) {
            return false;
        }
        common::Memory::EncryptMemory(ptr, size, key, keySize);
        return true;
    }

    bool MemoryEncryption::DecryptXor(void* ptr, size_t size, const uint8_t* key, size_t keySize)
    {
        if (!ptr || size == 0 || !key || keySize == 0) {
            return false;
        }
        common::Memory::DecryptMemory(ptr, size, key, keySize);
        return true;
    }

    // --- SideChannel ---
    SideChannel::SideChannel()
        : m_protectionEnabled(false)
    {
    }

    SideChannel::~SideChannel() = default;

    bool SideChannel::ConstantTimeCompare(const uint8_t* a, const uint8_t* b, size_t size)
    {
        return common::Memory::ConstantTimeCompare(a, b, size);
    }

    bool SideChannel::ConstantTimeCompare(uint64_t a, uint64_t b)
    {
        return a == b;
    }

    bool SideChannel::ConstantTimeCompare(const char* a, const char* b, size_t size)
    {
        return ConstantTimeCompare(
            reinterpret_cast<const uint8_t*>(a),
            reinterpret_cast<const uint8_t*>(b),
            size);
    }

    void SideChannel::SecureZero(void* ptr, size_t size)
    {
        common::Memory::SecureZero(ptr, size);
    }

    void SideChannel::SecureCopy(void* dest, const void* src, size_t size)
    {
        if (dest && src && size) {
            std::memmove(dest, src, size);
        }
    }

    void SideChannel::SecureFill(void* ptr, uint8_t value, size_t size)
    {
        if (ptr && size) {
            std::memset(ptr, static_cast<int>(value), size);
        }
    }

    void SideChannel::AntiTimingDelay(size_t operations)
    {
        (void)operations;
    }

    void SideChannel::RandomDelay()
    {
    }

    void SideChannel::FlushCache()
    {
    }

    void SideChannel::PreventCacheSideChannel()
    {
    }

    bool SideChannel::IsSideChannelProtectionEnabled()
    {
        return m_protectionEnabled;
    }

    void SideChannel::EnableSideChannelProtection()
    {
        m_protectionEnabled = true;
    }

    void SideChannel::DisableSideChannelProtection()
    {
        m_protectionEnabled = false;
    }

    // --- AntiDebug ---
    AntiDebug::AntiDebug()
        : m_enabled(false)
    {
    }

    AntiDebug::~AntiDebug() = default;

    bool AntiDebug::IsDebuggerPresent()
    {
#if defined(_WIN32)
        return ::IsDebuggerPresent() != FALSE;
#else
        // 非 Windows：可接 ptrace / proc；当前保守 false，避免 CI 误报。
        return false;
#endif
    }

    bool AntiDebug::IsHardwareBreakpointPresent()
    {
        return false;
    }

    bool AntiDebug::IsSoftwareBreakpointPresent()
    {
        return false;
    }

    bool AntiDebug::Enable()
    {
        m_enabled = true;
        return true;
    }

    void AntiDebug::Disable()
    {
        m_enabled = false;
    }

    bool AntiDebug::IsEnabled()
    {
        return m_enabled;
    }

    bool AntiDebug::DetectTimeBreakpoint()
    {
        return false;
    }

    bool AntiDebug::DetectMemoryBreakpoint()
    {
        return false;
    }

    bool AntiDebug::DetectThreadBreakpoint()
    {
        return false;
    }

    bool AntiDebug::DetectExceptionBreakpoint()
    {
        return false;
    }

    bool AntiDebug::ProtectCodeSection()
    {
        return false;
    }

    bool AntiDebug::ProtectDataSection()
    {
        return false;
    }

    bool AntiDebug::ObfuscateCode()
    {
        return false;
    }

    bool AntiDebug::CheckPEB()
    {
        return false;
    }

    bool AntiDebug::CheckProcessHeap()
    {
        return false;
    }

    bool AntiDebug::CheckThreadEnvironmentBlock()
    {
        return false;
    }

    bool AntiDebug::CheckDebugPort()
    {
        return false;
    }

    // --- JniSecurity ---
    JniSecurity::JniSecurity()
        : m_securityChecksEnabled(false)
    {
    }

    JniSecurity::~JniSecurity() = default;

    bool JniSecurity::ValidateCall(const void* jniEnv, const void* jclass)
    {
        return ValidateEnvironment(jniEnv) && ValidateClass(jniEnv, jclass);
    }

    bool JniSecurity::ValidateEnvironment(const void* jniEnv)
    {
        return jniEnv != nullptr;
    }

    bool JniSecurity::ValidateClass(const void* jniEnv, const void* jclass)
    {
        return jniEnv != nullptr && jclass != nullptr;
    }

    std::string JniSecurity::StringFromJni(const void* jniEnv, const void* jstring)
    {
#if defined(ZCHATIM_HAVE_JNI)
        auto* env = static_cast<JNIEnv*>(const_cast<void*>(jniEnv));
        auto* js  = static_cast<jstring>(const_cast<void*>(jstring));
        if (!env || !js) {
            return {};
        }
        if (env->ExceptionCheck()) {
            return {};
        }
        const char* utf = env->GetStringUTFChars(js, nullptr);
        if (!utf) {
            return {};
        }
        std::string out(utf);
        env->ReleaseStringUTFChars(js, utf);
        return out;
#else
        (void)jniEnv;
        (void)jstring;
        return {};
#endif
    }

    std::vector<uint8_t> JniSecurity::ByteArrayFromJni(const void* jniEnv, const void* jbyteArray)
    {
#if defined(ZCHATIM_HAVE_JNI)
        auto* env = static_cast<JNIEnv*>(const_cast<void*>(jniEnv));
        auto* arr = static_cast<jbyteArray>(const_cast<void*>(jbyteArray));
        if (!env || !arr) {
            return {};
        }
        if (env->ExceptionCheck()) {
            return {};
        }
        const jsize n = env->GetArrayLength(arr);
        if (n < 0) {
            return {};
        }
        std::vector<uint8_t> out(static_cast<size_t>(n));
        if (n > 0) {
            jbyte* body = env->GetByteArrayElements(arr, nullptr);
            if (!body) {
                return {};
            }
            std::memcpy(out.data(), body, static_cast<size_t>(n));
            env->ReleaseByteArrayElements(arr, body, JNI_ABORT);
        }
        return out;
#else
        (void)jniEnv;
        (void)jbyteArray;
        return {};
#endif
    }

    int32_t JniSecurity::IntFromJni(const void* jniEnv, const void* jvalue)
    {
#if defined(ZCHATIM_HAVE_JNI)
        auto* env = static_cast<JNIEnv*>(const_cast<void*>(jniEnv));
        auto* jo  = static_cast<jobject>(const_cast<void*>(jvalue));
        if (!env || !jo) {
            return 0;
        }
        jclass cls = env->GetObjectClass(jo);
        if (!cls) {
            return 0;
        }
        jmethodID mid = env->GetMethodID(cls, "intValue", "()I");
        if (!mid) {
            return 0;
        }
        return env->CallIntMethod(jo, mid);
#else
        (void)jniEnv;
        (void)jvalue;
        return 0;
#endif
    }

    int64_t JniSecurity::LongFromJni(const void* jniEnv, const void* jvalue)
    {
#if defined(ZCHATIM_HAVE_JNI)
        auto* env = static_cast<JNIEnv*>(const_cast<void*>(jniEnv));
        auto* jo  = static_cast<jobject>(const_cast<void*>(jvalue));
        if (!env || !jo) {
            return 0;
        }
        jclass cls = env->GetObjectClass(jo);
        if (!cls) {
            return 0;
        }
        jmethodID mid = env->GetMethodID(cls, "longValue", "()J");
        if (!mid) {
            return 0;
        }
        return env->CallLongMethod(jo, mid);
#else
        (void)jniEnv;
        (void)jvalue;
        return 0;
#endif
    }

    void* JniSecurity::AllocateJniMemory(const void* jniEnv, size_t size)
    {
        (void)jniEnv;
        (void)size;
        return nullptr;
    }

    void JniSecurity::FreeJniMemory(const void* jniEnv, void* ptr)
    {
        (void)jniEnv;
        (void)ptr;
    }

    bool JniSecurity::CheckException(const void* jniEnv)
    {
#if defined(ZCHATIM_HAVE_JNI)
        auto* env = static_cast<JNIEnv*>(const_cast<void*>(jniEnv));
        return env != nullptr && env->ExceptionCheck() != JNI_FALSE;
#else
        (void)jniEnv;
        return false;
#endif
    }

    void JniSecurity::ClearException(const void* jniEnv)
    {
#if defined(ZCHATIM_HAVE_JNI)
        auto* env = static_cast<JNIEnv*>(const_cast<void*>(jniEnv));
        if (env) {
            env->ExceptionClear();
        }
#else
        (void)jniEnv;
#endif
    }

    bool JniSecurity::HandleException(const void* jniEnv)
    {
#if defined(ZCHATIM_HAVE_JNI)
        auto* env = static_cast<JNIEnv*>(const_cast<void*>(jniEnv));
        if (!env) {
            return false;
        }
        if (!env->ExceptionCheck()) {
            return false;
        }
        env->ExceptionClear();
        return true;
#else
        (void)jniEnv;
        return false;
#endif
    }

    void JniSecurity::EnableSecurityChecks()
    {
        m_securityChecksEnabled = true;
    }

    void JniSecurity::DisableSecurityChecks()
    {
        m_securityChecksEnabled = false;
    }

    bool JniSecurity::IsSecurityChecksEnabled()
    {
        return m_securityChecksEnabled;
    }

    bool JniSecurity::IsValidJniEnv(const void* jniEnv)
    {
        return jniEnv != nullptr;
    }

    bool JniSecurity::IsValidJniClass(const void* jniEnv, const void* jclass)
    {
        return jniEnv != nullptr && jclass != nullptr;
    }

    // --- KeyManagement ---
    KeyManagement::KeyManagement()  = default;
    KeyManagement::~KeyManagement() = default;

    namespace {

        constexpr size_t kMaxKeyMaterialBytes = 64ULL * 1024ULL * 1024ULL;

        bool vectorAllZero(const std::vector<uint8_t>& v)
        {
            for (uint8_t b : v) {
                if (b != 0) {
                    return false;
                }
            }
            return true;
        }

        std::mutex& secureInt64Mutex()
        {
            static std::mutex m;
            return m;
        }

        std::mt19937_64& secureInt64Engine()
        {
            static std::mt19937_64 eng([]() -> uint64_t {
                auto b = mm2::Crypto::GenerateSecureRandom(sizeof(uint64_t));
                uint64_t s = 0;
                if (b.size() == sizeof(uint64_t)) {
                    std::memcpy(&s, b.data(), sizeof(s));
                }
                return s;
            }());
            return eng;
        }

    } // namespace

    std::vector<uint8_t> KeyManagement::GenerateMasterKey()
    {
        return mm2::Crypto::GenerateSecureRandom(CRYPTO_KEY_SIZE);
    }

    std::vector<uint8_t> KeyManagement::GenerateSessionKey()
    {
        return mm2::Crypto::GenerateSecureRandom(CRYPTO_KEY_SIZE);
    }

    std::vector<uint8_t> KeyManagement::GenerateMessageKey()
    {
        return mm2::Crypto::GenerateSecureRandom(CRYPTO_KEY_SIZE);
    }

    std::vector<uint8_t> KeyManagement::GenerateRandomKey(size_t keySize)
    {
        if (keySize == 0 || keySize > kMaxKeyMaterialBytes) {
            return {};
        }
        return mm2::Crypto::GenerateSecureRandom(keySize);
    }

    std::vector<uint8_t> KeyManagement::DeriveKey(
        const std::vector<uint8_t>& inputKey,
        const std::vector<uint8_t>& salt)
    {
        return DeriveKey(inputKey, salt, CRYPTO_KEY_SIZE);
    }

    std::vector<uint8_t> KeyManagement::DeriveKey(
        const std::vector<uint8_t>& inputKey,
        const std::vector<uint8_t>& salt,
        size_t                      outputKeySize)
    {
        if (inputKey.empty() || salt.empty() || outputKeySize == 0 || outputKeySize > kMaxKeyMaterialBytes) {
            return {};
        }
        if (!mm2::Crypto::Init()) {
            return {};
        }
        std::vector<uint8_t> out(outputKeySize);
        if (!mm2::Crypto::DeriveKey(
                inputKey.data(),
                inputKey.size(),
                salt.data(),
                salt.size(),
                out.data(),
                out.size())) {
            return {};
        }
        return out;
    }

    bool KeyManagement::StoreMasterKey(const std::vector<uint8_t>& key)
    {
        m_masterKey = key;
        return true;
    }

    std::vector<uint8_t> KeyManagement::GetMasterKey()
    {
        return m_masterKey;
    }

    void KeyManagement::ClearMasterKey()
    {
        m_masterKey.clear();
    }

    std::vector<uint8_t> KeyManagement::RefreshMasterKey()
    {
        std::vector<uint8_t> k = GenerateMasterKey();
        if (!k.empty()) {
            (void)StoreMasterKey(k);
        }
        return k;
    }

    std::vector<uint8_t> KeyManagement::RefreshSessionKey()
    {
        return GenerateSessionKey();
    }

    bool KeyManagement::ValidateKey(const std::vector<uint8_t>& key)
    {
        return !key.empty() && IsValidKeySize(key.size());
    }

    bool KeyManagement::CheckKeyStrength(const std::vector<uint8_t>& key)
    {
        return ValidateKey(key) && !vectorAllZero(key);
    }

    bool KeyManagement::ExportKey(
        const std::vector<uint8_t>& key,
        const std::string&          filePath,
        const std::string&          password)
    {
        (void)key;
        (void)filePath;
        (void)password;
        return false;
    }

    std::vector<uint8_t> KeyManagement::ImportKey(
        const std::string& filePath,
        const std::string& password)
    {
        (void)filePath;
        (void)password;
        return {};
    }

    bool KeyManagement::IsValidKeySize(size_t keySize)
    {
        return keySize == CRYPTO_KEY_SIZE;
    }

    bool KeyManagement::IsStrongKey(const std::vector<uint8_t>& key)
    {
        return CheckKeyStrength(key);
    }

    // --- SecureRandom ---
    SecureRandom::SecureRandom()
        : m_initialized(false)
    {
    }

    SecureRandom::~SecureRandom() = default;

    std::vector<uint8_t> SecureRandom::Generate(size_t size)
    {
        return common::Random::GenerateSecureBytes(size);
    }

    bool SecureRandom::Generate(uint8_t* buffer, size_t size)
    {
        if (!buffer || size == 0) {
            return false;
        }
        const auto v = common::Random::GenerateSecureBytes(size);
        if (v.size() != size) {
            return false;
        }
        std::memcpy(buffer, v.data(), size);
        return true;
    }

    int32_t SecureRandom::GenerateInt(int32_t min, int32_t max)
    {
        return common::Random::GenerateSecureInt(min, max);
    }

    uint32_t SecureRandom::GenerateUInt(uint32_t min, uint32_t max)
    {
        return common::Random::GenerateSecureUInt(min, max);
    }

    int64_t SecureRandom::GenerateInt64(int64_t min, int64_t max)
    {
        if (min > max) {
            return 0;
        }
        std::lock_guard<std::mutex> lk(secureInt64Mutex());
        std::uniform_int_distribution<int64_t> dist(min, max);
        return dist(secureInt64Engine());
    }

    uint64_t SecureRandom::GenerateUInt64(uint64_t min, uint64_t max)
    {
        if (min > max) {
            return 0;
        }
        std::lock_guard<std::mutex> lk(secureInt64Mutex());
        std::uniform_int_distribution<uint64_t> dist(min, max);
        return dist(secureInt64Engine());
    }

    bool SecureRandom::GenerateBool()
    {
        const auto v = common::Random::GenerateSecureBytes(1);
        return !v.empty() && ((v[0] & 1U) != 0);
    }

    std::vector<uint8_t> SecureRandom::GenerateMessageId()
    {
        return common::Random::GenerateMessageId();
    }

    std::vector<uint8_t> SecureRandom::GenerateSessionId()
    {
        return common::Random::GenerateSessionId();
    }

    std::string SecureRandom::GenerateFileId()
    {
        return common::Random::GenerateFileId();
    }

    std::string SecureRandom::GenerateRandomString(size_t length)
    {
        return common::Random::GenerateRandomString(length);
    }

    bool SecureRandom::CheckQuality()
    {
        const auto v = common::Random::GenerateSecureBytes(8);
        return v.size() == 8;
    }

    double SecureRandom::GetEntropy()
    {
        return 8.0;
    }

    bool SecureRandom::Initialize()
    {
        m_initialized = true;
        return true;
    }

    void SecureRandom::Cleanup()
    {
        m_initialized = false;
    }

    bool SecureRandom::IsInitialized()
    {
        return m_initialized;
    }

    bool SecureRandom::InitSystemRandom()
    {
        return CheckQuality();
    }

    bool SecureRandom::InitHardwareRandom()
    {
        return false;
    }

} // namespace ZChatIM::mm1::security
