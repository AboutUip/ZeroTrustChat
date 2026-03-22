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

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <cstdint>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#if defined(__x86_64__) || defined(_M_AMD64) || defined(__i386__) || defined(_M_IX86)
#    if !defined(__aarch64__) && !defined(_M_ARM64) && !defined(_M_ARM64EC)
#        include <immintrin.h>
#        define ZCHATIM_SIDECHANNEL_CLFLUSH 1
#    endif
#endif

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

namespace {
// 4 KiB scratch for `SideChannel::FlushCache`: line-sized reads + optional x86 CLFLUSH.
alignas(64) std::uint8_t g_sideChannelScratch[4096]{};
} // namespace

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
        const auto lkIt = m_lockedRegions.find(ptr);
        if (lkIt != m_lockedRegions.end()) {
            (void)common::Memory::UnlockMemory(ptr, lkIt->second);
            m_lockedRegions.erase(lkIt);
        }
        m_allocatedMemory.erase(ptr);
        common::Memory::Free(ptr);
    }

    void* SecurityMemory::Reallocate(void* ptr, size_t newSize)
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (ptr) {
            const auto lkIt = m_lockedRegions.find(ptr);
            if (lkIt != m_lockedRegions.end()) {
                (void)common::Memory::UnlockMemory(ptr, lkIt->second);
                m_lockedRegions.erase(lkIt);
            }
        }
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
        if (!common::Memory::LockMemory(ptr, size)) {
            return false;
        }
        std::lock_guard<std::mutex> lk(m_mutex);
        m_lockedRegions[ptr] = size;
        return true;
    }

    bool SecurityMemory::Unlock(void* ptr, size_t size)
    {
        if (!ptr || size == 0) {
            return false;
        }
        const bool ok = common::Memory::UnlockMemory(ptr, size);
        std::lock_guard<std::mutex> lk(m_mutex);
        const auto it = m_lockedRegions.find(ptr);
        if (ok && it != m_lockedRegions.end() && it->second == size) {
            m_lockedRegions.erase(it);
        }
        return ok;
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

    bool SecurityMemory::IsLocked(const void* ptr, size_t size) const
    {
        if (!ptr || size == 0) {
            return false;
        }
        const auto q   = reinterpret_cast<std::uintptr_t>(ptr);
        const auto qEnd = q + size;
        if (qEnd < q) {
            return false;
        }
        std::lock_guard<std::mutex> lk(m_mutex);
        for (const auto& pr : m_lockedRegions) {
            const auto p    = reinterpret_cast<std::uintptr_t>(pr.first);
            const auto pEnd = p + pr.second;
            if (pEnd < p) {
                continue;
            }
            if (p <= q && qEnd <= pEnd) {
                return true;
            }
        }
        return false;
    }

    void SecurityMemory::ReleaseAllLockTracking()
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        for (const auto& pr : m_lockedRegions) {
            (void)common::Memory::UnlockMemory(pr.first, pr.second);
        }
        m_lockedRegions.clear();
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
        return common::Memory::ConstantTimeCompare(
            reinterpret_cast<const uint8_t*>(&a),
            reinterpret_cast<const uint8_t*>(&b),
            sizeof(uint64_t));
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
        common::Memory::SecureCopy(dest, src, size);
    }

    void SideChannel::SecureFill(void* ptr, uint8_t value, size_t size)
    {
        common::Memory::SecureFill(ptr, value, size);
    }

    void SideChannel::AntiTimingDelay(size_t operations)
    {
        // 有界防 DoS：忽略超大 `operations`；与 `m_protectionEnabled` 联动加重工作量。
        constexpr size_t kCap = 65536;
        size_t            n     = std::min(operations, kCap);
        if (m_protectionEnabled) {
            n *= 32U;
        } else {
            n *= 4U;
        }
        volatile std::uint64_t acc = 0;
        for (size_t i = 0; i < n; ++i) {
            acc ^= static_cast<std::uint64_t>(i) * UINT64_C(1315423911);
        }
        (void)acc;
        std::atomic_thread_fence(std::memory_order_seq_cst);
    }

    void SideChannel::RandomDelay()
    {
        // CSPRNG 抖动；防护关时仍用较短上界，降低默认同步路径成本。
        std::vector<std::uint8_t> bytes = common::Random::GenerateSecureBytes(2);
        const std::uint32_t       span  = m_protectionEnabled ? 8000U : 400U;
        const std::uint32_t       base  = m_protectionEnabled ? 40U : 8U;
        std::uint32_t             us    = base;
        if (bytes.size() >= 2) {
            const std::uint32_t r = static_cast<std::uint32_t>(bytes[0])
                | (static_cast<std::uint32_t>(bytes[1]) << 8);
            us = base + (r % span);
        }
        std::this_thread::sleep_for(std::chrono::microseconds(static_cast<std::chrono::microseconds::rep>(us)));
    }

    void SideChannel::FlushCache()
    {
        std::atomic_thread_fence(std::memory_order_seq_cst);
#if defined(ZCHATIM_SIDECHANNEL_CLFLUSH)
        for (size_t i = 0; i < sizeof(g_sideChannelScratch); i += 64U) {
            _mm_clflush(reinterpret_cast<const void*>(&g_sideChannelScratch[i]));
        }
#endif
        volatile std::uint32_t sink = 0;
        for (size_t i = 0; i < sizeof(g_sideChannelScratch); i += 64U) {
            sink ^= g_sideChannelScratch[i];
        }
        (void)sink;
        std::atomic_thread_fence(std::memory_order_seq_cst);
    }

    void SideChannel::PreventCacheSideChannel()
    {
        FlushCache();
        if (m_protectionEnabled) {
            AntiTimingDelay(256);
        }
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

    bool JniSecurity::ValidateCall(const void* jniEnv, const void* jcls)
    {
        return ValidateEnvironment(jniEnv) && ValidateClass(jniEnv, jcls);
    }

    bool JniSecurity::ValidateEnvironment(const void* jniEnv)
    {
        return jniEnv != nullptr;
    }

    bool JniSecurity::ValidateClass(const void* jniEnv, const void* jcls)
    {
        return jniEnv != nullptr && jcls != nullptr;
    }

    std::string JniSecurity::StringFromJni(const void* jniEnv, const void* jstr)
    {
#if defined(ZCHATIM_HAVE_JNI)
        auto* env = static_cast<JNIEnv*>(const_cast<void*>(jniEnv));
        auto* js  = static_cast<jstring>(const_cast<void*>(jstr));
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
        (void)jstr;
        return {};
#endif
    }

    std::vector<uint8_t> JniSecurity::ByteArrayFromJni(const void* jniEnv, const void* jbytes)
    {
#if defined(ZCHATIM_HAVE_JNI)
        auto* env = static_cast<JNIEnv*>(const_cast<void*>(jniEnv));
        auto* arr = static_cast<jbyteArray>(const_cast<void*>(jbytes));
        if (!env || !arr) {
            return {};
        }
        if (env->ExceptionCheck()) {
            return {};
        }
        const jsize len = env->GetArrayLength(arr);
        if (len < 0) {
            return {};
        }
        std::vector<uint8_t> out(static_cast<size_t>(len));
        if (len > 0) {
            jbyte* body = env->GetByteArrayElements(arr, nullptr);
            if (!body) {
                return {};
            }
            std::memcpy(out.data(), body, static_cast<size_t>(len));
            env->ReleaseByteArrayElements(arr, body, JNI_ABORT);
        }
        return out;
#else
        (void)jniEnv;
        (void)jbytes;
        return {};
#endif
    }

    int32_t JniSecurity::IntFromJni(const void* jniEnv, const void* jobj)
    {
#if defined(ZCHATIM_HAVE_JNI)
        auto* env = static_cast<JNIEnv*>(const_cast<void*>(jniEnv));
        auto* jo  = static_cast<jobject>(const_cast<void*>(jobj));
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
        (void)jobj;
        return 0;
#endif
    }

    int64_t JniSecurity::LongFromJni(const void* jniEnv, const void* jobj)
    {
#if defined(ZCHATIM_HAVE_JNI)
        auto* env = static_cast<JNIEnv*>(const_cast<void*>(jniEnv));
        auto* jo  = static_cast<jobject>(const_cast<void*>(jobj));
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
        (void)jobj;
        return 0;
#endif
    }

    void* JniSecurity::AllocateJniMemory(const void* jniEnv, size_t size)
    {
        (void)jniEnv;
        if (size == 0) {
            return nullptr;
        }
        void* p = common::Memory::Allocate(size);
        if (p) {
            common::Memory::SecureZero(p, size);
        }
        return p;
    }

    void JniSecurity::FreeJniMemory(const void* jniEnv, void* ptr)
    {
        (void)jniEnv;
        if (ptr) {
            common::Memory::Free(ptr);
        }
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

    bool JniSecurity::IsValidJniClass(const void* jniEnv, const void* jcls)
    {
        return jniEnv != nullptr && jcls != nullptr;
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
