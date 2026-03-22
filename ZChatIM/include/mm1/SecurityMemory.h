#pragma once

#include "../Types.h"
#include <cstdint>
#include <map>
#include <mutex>

namespace ZChatIM
{
    namespace mm1
    {
        namespace security
        {
            class SecurityMemory {
            public:
                SecurityMemory();
                ~SecurityMemory();

                void* Allocate(size_t size);

                void Free(void* ptr);

                void* Reallocate(void* ptr, size_t newSize);

                bool Lock(void* ptr, size_t size);

                bool Unlock(void* ptr, size_t size);

                bool Protect(void* ptr, size_t size, int protection);

                bool IsAccessible(const void* ptr, size_t size);

                bool IsLocked(const void* ptr, size_t size) const;

                size_t GetAllocatedSize() const;

                size_t GetPeakMemoryUsage() const;

                void ResetMemoryStats();

                void ReleaseAllLockTracking();

            private:
                SecurityMemory(const SecurityMemory&) = delete;
                SecurityMemory& operator=(const SecurityMemory&) = delete;

                mutable std::mutex m_mutex;
                std::map<void*, size_t> m_allocatedMemory;
                std::map<void*, size_t> m_lockedRegions;
                size_t m_totalAllocated;
                size_t m_peakUsage;
            };

        } // namespace security
    } // namespace mm1
} // namespace ZChatIM
