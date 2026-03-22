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
            // =============================================================
            // 安全内存操作
            // =============================================================
            
            class SecurityMemory {
            public:
                // =============================================================
                // 构造函数/析构函数
                // =============================================================
                
                SecurityMemory();
                ~SecurityMemory();
                
                // =============================================================
                // 内存分配/释放
                // =============================================================
                
                // 分配安全内存
                void* Allocate(size_t size);
                
                // 释放安全内存
                void Free(void* ptr);
                
                // 重新分配内存
                void* Reallocate(void* ptr, size_t newSize);
                
                // =============================================================
                // 内存保护
                // =============================================================
                
                // 锁定内存（防止交换到磁盘）
                bool Lock(void* ptr, size_t size);
                
                // 解锁内存
                bool Unlock(void* ptr, size_t size);
                
                // 设置内存保护
                bool Protect(void* ptr, size_t size, int protection);
                
                // =============================================================
                // 内存检查
                // =============================================================
                
                // 检查内存是否可访问
                bool IsAccessible(const void* ptr, size_t size);
                
                // 检查内存是否已锁定（查询区间须**完全落在**某次成功 **`Lock(ptr,size)`** 的登记范围内）
                bool IsLocked(const void* ptr, size_t size) const;
                
                // =============================================================
                // 内存统计
                // =============================================================
                
                // 获取已分配内存大小
                size_t GetAllocatedSize() const;
                
                // 获取峰值内存使用
                size_t GetPeakMemoryUsage() const;
                
                // 重置内存统计
                void ResetMemoryStats();

                /// 紧急擦除等路径：**尽力**对已跟踪的 **`Lock`** 区间调用 OS **`Unlock`** 并清空本书（**不** `Free` 分配块）。
                void ReleaseAllLockTracking();
                
            private:
                // 禁止实例化
                SecurityMemory(const SecurityMemory&) = delete;
                SecurityMemory& operator=(const SecurityMemory&) = delete;
                
                // 成员变量
                mutable std::mutex m_mutex;
                std::map<void*, size_t> m_allocatedMemory;
                /// 由 **`Lock` 成功**登记的区间 **[ptr, ptr+size)`**（与 OS **`VirtualLock`/`mlock`** 一致）。
                std::map<void*, size_t> m_lockedRegions;
                size_t m_totalAllocated;
                size_t m_peakUsage;
            };
            
        } // namespace security
    } // namespace mm1
} // namespace ZChatIM
