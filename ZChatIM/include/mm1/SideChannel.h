#pragma once

#include "../Types.h"
#include <cstdint>

namespace ZChatIM
{
    namespace mm1
    {
        namespace security
        {
            // =============================================================
            // 侧信道防护
            // =============================================================
            
            class SideChannel {
            public:
                // =============================================================
                // 构造函数/析构函数
                // =============================================================
                
                SideChannel();
                ~SideChannel();
                
                // =============================================================
                // 常量时间比较
                // =============================================================
                
                // 常量时间比较（字节数组）
                bool ConstantTimeCompare(const uint8_t* a, const uint8_t* b, size_t size);
                
                // 常量时间比较（整数）
                bool ConstantTimeCompare(uint64_t a, uint64_t b);
                
                // 常量时间比较（字符串）
                bool ConstantTimeCompare(const char* a, const char* b, size_t size);
                
                // =============================================================
                // 安全内存操作
                // =============================================================
                
                // 安全内存清零（防止编译器优化）
                void SecureZero(void* ptr, size_t size);
                
                // 安全内存拷贝
                void SecureCopy(void* dest, const void* src, size_t size);
                
                // 安全内存填充
                void SecureFill(void* ptr, uint8_t value, size_t size);
                
                // =============================================================
                // 时间防护
                // =============================================================
                
                // 防时间攻击延迟
                void AntiTimingDelay(size_t operations);
                
                // 随机延迟
                void RandomDelay();
                
                // =============================================================
                // 缓存防护
                // =============================================================
                
                // 清理缓存
                void FlushCache();
                
                // 防止缓存侧信道
                void PreventCacheSideChannel();
                
                // =============================================================
                // 辅助方法
                // =============================================================
                
                // 检查是否启用侧信道防护
                bool IsSideChannelProtectionEnabled();
                
                // 启用侧信道防护
                void EnableSideChannelProtection();
                
                // 禁用侧信道防护
                void DisableSideChannelProtection();
                
            private:
                // 禁止实例化
                SideChannel(const SideChannel&) = delete;
                SideChannel& operator=(const SideChannel&) = delete;
                
                // 成员变量
                bool m_protectionEnabled;
            };
            
        } // namespace security
    } // namespace mm1
} // namespace ZChatIM
