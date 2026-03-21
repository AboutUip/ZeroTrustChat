#include "common/Memory.h"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <limits>

#if defined(_WIN32)
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <malloc.h>
#    include <windows.h>
#else
#    include <sys/mman.h>
#    include <unistd.h>
#endif

namespace ZChatIM::common {

    namespace {

        constexpr size_t kHeaderSz = sizeof(size_t);

        uint8_t* HeaderPtr(void* userPtr)
        {
            return static_cast<uint8_t*>(userPtr) - static_cast<ptrdiff_t>(kHeaderSz);
        }

        size_t ReadUserSize(void* userPtr)
        {
            size_t sz = 0;
            std::memcpy(&sz, HeaderPtr(userPtr), sizeof(sz));
            return sz;
        }

        std::atomic<size_t> g_allocatedSize{0};
        std::atomic<size_t> g_peakMemoryUsage{0};

        void AdjustPeak()
        {
            const size_t cur = g_allocatedSize.load(std::memory_order_relaxed);
            size_t peak = g_peakMemoryUsage.load(std::memory_order_relaxed);
            while (cur > peak && !g_peakMemoryUsage.compare_exchange_weak(peak, cur, std::memory_order_relaxed)) {
            }
        }

    } // namespace

    void* Memory::Allocate(size_t size)
    {
        if (size == 0)
            return nullptr;
        if (size > (std::numeric_limits<size_t>::max)() - kHeaderSz)
            return nullptr;
        const size_t total = kHeaderSz + size;
        void* raw = std::malloc(total);
        if (!raw)
            return nullptr;
        std::memcpy(raw, &size, sizeof(size_t));
        void* user = static_cast<uint8_t*>(raw) + kHeaderSz;
        const size_t add = size;
        g_allocatedSize.fetch_add(add, std::memory_order_relaxed);
        AdjustPeak();
        return user;
    }

    void Memory::Free(void* ptr)
    {
        if (!ptr)
            return;
        void* raw = HeaderPtr(ptr);
        const size_t sz = ReadUserSize(ptr);
        std::free(raw);
        g_allocatedSize.fetch_sub(sz, std::memory_order_relaxed);
    }

    void* Memory::Reallocate(void* ptr, size_t newSize)
    {
        if (newSize == 0) {
            Free(ptr);
            return nullptr;
        }
        if (!ptr)
            return Allocate(newSize);
        const size_t oldSz = ReadUserSize(ptr);
        void* raw = HeaderPtr(ptr);
        if (newSize > (std::numeric_limits<size_t>::max)() - kHeaderSz)
            return nullptr;
        const size_t total = kHeaderSz + newSize;
        void* newRaw = std::realloc(raw, total);
        if (!newRaw)
            return nullptr;
        std::memcpy(newRaw, &newSize, sizeof(size_t));
        void* user = static_cast<uint8_t*>(newRaw) + kHeaderSz;
        const int64_t delta = static_cast<int64_t>(newSize) - static_cast<int64_t>(oldSz);
        if (delta > 0)
            g_allocatedSize.fetch_add(static_cast<size_t>(delta), std::memory_order_relaxed);
        else if (delta < 0)
            g_allocatedSize.fetch_sub(static_cast<size_t>(-delta), std::memory_order_relaxed);
        AdjustPeak();
        return user;
    }

    void Memory::SecureZero(void* data, size_t length)
    {
        if (!data || length == 0)
            return;
#if defined(_WIN32)
        SecureZeroMemory(data, length);
#else
        volatile unsigned char* p = static_cast<volatile unsigned char*>(data);
        while (length--)
            *p++ = 0;
#endif
    }

    bool Memory::ConstantTimeCompare(const void* a, const void* b, size_t length)
    {
        if (!a || !b)
            return length == 0;
        const auto* x = static_cast<const unsigned char*>(a);
        const auto* y = static_cast<const unsigned char*>(b);
        unsigned char diff = 0;
        for (size_t i = 0; i < length; ++i)
            diff |= static_cast<unsigned char>(x[i] ^ y[i]);
        return diff == 0;
    }

    void Memory::SecureCopy(void* dest, const void* src, size_t length)
    {
        if (!dest || !src || length == 0)
            return;
        std::memcpy(dest, src, length);
    }

    void Memory::SecureFill(void* data, uint8_t value, size_t length)
    {
        if (!data || length == 0)
            return;
        std::memset(data, static_cast<int>(value), length);
    }

    bool Memory::LockMemory(void* ptr, size_t length)
    {
        if (!ptr || length == 0)
            return false;
#if defined(_WIN32)
        return VirtualLock(ptr, length) != FALSE;
#else
        return mlock(ptr, length) == 0;
#endif
    }

    bool Memory::UnlockMemory(void* ptr, size_t length)
    {
        if (!ptr || length == 0)
            return false;
#if defined(_WIN32)
        return VirtualUnlock(ptr, length) != FALSE;
#else
        return munlock(ptr, length) == 0;
#endif
    }

    bool Memory::ProtectMemory(void* ptr, size_t length, int protection)
    {
        if (!ptr || length == 0)
            return false;
#if defined(_WIN32)
        DWORD old = 0;
        DWORD prot = PAGE_READWRITE;
        if (protection == 0)
            prot = PAGE_READONLY;
        else if (protection == 1)
            prot = PAGE_NOACCESS;
        return VirtualProtect(ptr, length, prot, &old) != FALSE;
#else
        (void)protection;
        return false;
#endif
    }

    bool Memory::IsMemoryAccessible(const void* ptr, size_t length)
    {
        if (!ptr || length == 0)
            return false;
#if defined(_WIN32)
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(ptr, &mbi, sizeof(mbi)) == 0)
            return false;
        return mbi.Protect != PAGE_NOACCESS;
#else
        (void)length;
        return true;
#endif
    }

    bool Memory::IsMemoryInitialized(const void* ptr, size_t length)
    {
        (void)length;
        return ptr != nullptr;
    }

    bool Memory::IsMemoryZero(const void* ptr, size_t length)
    {
        if (!ptr)
            return length == 0;
        const auto* p = static_cast<const unsigned char*>(ptr);
        for (size_t i = 0; i < length; ++i) {
            if (p[i] != 0)
                return false;
        }
        return true;
    }

    size_t Memory::GetAllocatedSize()
    {
        return g_allocatedSize.load(std::memory_order_relaxed);
    }

    size_t Memory::GetPeakMemoryUsage()
    {
        return g_peakMemoryUsage.load(std::memory_order_relaxed);
    }

    void Memory::ResetMemoryStats()
    {
        const size_t cur = g_allocatedSize.load(std::memory_order_relaxed);
        g_peakMemoryUsage.store(cur, std::memory_order_relaxed);
    }

    void Memory::EncryptMemory(void* data, size_t length, const uint8_t* key, size_t keyLength)
    {
        if (!data || !key || length == 0 || keyLength == 0)
            return;
        auto* p = static_cast<uint8_t*>(data);
        for (size_t i = 0; i < length; ++i)
            p[i] ^= key[i % keyLength];
    }

    void Memory::DecryptMemory(void* data, size_t length, const uint8_t* key, size_t keyLength)
    {
        EncryptMemory(data, length, key, keyLength);
    }

    void* Memory::AlignMemory(void* ptr, size_t alignment)
    {
        if (!ptr || alignment == 0 || (alignment & (alignment - 1)) != 0)
            return ptr;
        const uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
        const uintptr_t aligned = (addr + alignment - 1) & ~(alignment - 1);
        return reinterpret_cast<void*>(aligned);
    }

    void* Memory::AllocateAligned(size_t size, size_t alignment)
    {
        if (size == 0 || alignment == 0 || (alignment & (alignment - 1)) != 0)
            return nullptr;
#if defined(_WIN32)
        return _aligned_malloc(size, alignment);
#else
        void* p = nullptr;
        if (posix_memalign(&p, alignment, size) != 0)
            return nullptr;
        return p;
#endif
    }

    void Memory::FreeAligned(void* ptr)
    {
        if (!ptr)
            return;
#if defined(_WIN32)
        _aligned_free(ptr);
#else
        std::free(ptr);
#endif
    }

    MemoryGuard::MemoryGuard(void* ptr, size_t length)
        : m_ptr(ptr)
        , m_length(length)
    {
        (void)Memory::LockMemory(m_ptr, m_length);
    }

    MemoryGuard::~MemoryGuard()
    {
        (void)Memory::UnlockMemory(m_ptr, m_length);
    }

} // namespace ZChatIM::common
