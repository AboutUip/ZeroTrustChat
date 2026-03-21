#pragma once

#include <vector>
#include <cstdint>
#include <cstddef>

namespace ZChatIM
{
    namespace common
    {
        // =============================================================
        // 内存工具类
        // =============================================================
        //
        // **线程安全**：`Allocate` / `Free` / `Reallocate` 及统计 API 使用 **原子** 更新；
        // 与 `malloc` 一致，**同一块内存** 不应跨线程并发 `Free`/`Reallocate`（由调用方保证）。
        //
        class Memory {
        public:
            // =============================================================
            // 内存分配/释放
            // =============================================================
            
            // 分配内存（**`size` 过大导致 `size+sizeof(size_t)` 溢出** 时返回 **nullptr**）
            static void* Allocate(size_t size);
            
            // 释放内存
            static void Free(void* ptr);
            
            // 重新分配内存（**`newSize` 过大导致与头部长度相加溢出** 时返回 **nullptr**，原块保留）
            static void* Reallocate(void* ptr, size_t newSize);
            
            // =============================================================
            // 安全内存操作
            // =============================================================
            
            // 安全内存清零（防止编译器优化）
            static void SecureZero(void* data, size_t length);
            
            // 安全内存比较（常量时间）
            static bool ConstantTimeCompare(const void* a, const void* b, size_t length);
            
            // 安全内存拷贝
            static void SecureCopy(void* dest, const void* src, size_t length);
            
            // 安全内存填充
            static void SecureFill(void* data, uint8_t value, size_t length);
            
            // =============================================================
            // 内存保护
            // =============================================================
            
            // 锁定内存（防止交换到磁盘）
            static bool LockMemory(void* ptr, size_t length);
            
            // 解锁内存
            static bool UnlockMemory(void* ptr, size_t length);
            
            // 设置内存保护（Windows：`VirtualProtect`；**0**=只读 **PAGE_READONLY**，**1**=不可访问 **PAGE_NOACCESS**，其它=读写 **PAGE_READWRITE**；非 Windows：暂返回 **false**）
            static bool ProtectMemory(void* ptr, size_t length, int protection);
            
            // =============================================================
            // 内存检查
            // =============================================================
            
            // 检查内存是否可访问
            static bool IsMemoryAccessible(const void* ptr, size_t length);
            
            // 是否已初始化：**当前实现**仅等价于 `ptr != nullptr`（无法检测内容是否已写入）
            static bool IsMemoryInitialized(const void* ptr, size_t length);
            
            // 检查内存是否为零
            static bool IsMemoryZero(const void* ptr, size_t length);
            
            // =============================================================
            // 内存统计
            // =============================================================
            
            // 获取已分配内存大小（**仅**统计 **`Allocate` / `Free` / `Reallocate`** 带内嵌长度头的路径；**不含** **`AllocateAligned`**）
            static size_t GetAllocatedSize();
            
            // 获取峰值内存使用（同上统计口径）
            static size_t GetPeakMemoryUsage();
            
            // 重置内存统计
            static void ResetMemoryStats();
            
            // =============================================================
            // 内存加密
            // =============================================================
            
            // 内存加密（简单XOR）
            static void EncryptMemory(void* data, size_t length, const uint8_t* key, size_t keyLength);
            
            // 内存解密（简单XOR）
            static void DecryptMemory(void* data, size_t length, const uint8_t* key, size_t keyLength);
            
            // =============================================================
            // 内存对齐
            // =============================================================
            
            // 对齐内存地址
            static void* AlignMemory(void* ptr, size_t alignment);
            
            // 分配对齐内存
            static void* AllocateAligned(size_t size, size_t alignment);
            
            // 释放对齐内存
            static void FreeAligned(void* ptr);
            
        private:
            // 禁止实例化
            Memory() = delete;
            ~Memory() = delete;
        };
        
        // =============================================================
        // 内存保护类
        // =============================================================
        
        class MemoryGuard {
        public:
            // 构造函数
            MemoryGuard(void* ptr, size_t length);
            
            // 析构函数
            ~MemoryGuard();
            
            // 禁止复制和赋值
            MemoryGuard(const MemoryGuard&) = delete;
            MemoryGuard& operator=(const MemoryGuard&) = delete;
            
        private:
            void* m_ptr;
            size_t m_length;
        };
        
    } // namespace common
} // namespace ZChatIM
