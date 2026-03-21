#pragma once

#include <string>
#include <cstdint>

namespace ZChatIM
{
    namespace common
    {
        // =============================================================
        // 时间工具类
        // =============================================================
        
        class Time {
        public:
            // =============================================================
            // 时间戳获取
            // =============================================================
            
            // 获取当前时间戳（毫秒）
            static uint64_t GetCurrentTimestamp();
            
            // 获取当前时间戳（秒）
            static uint64_t GetCurrentTimestampSeconds();
            
            // =============================================================
            // 时间戳转换
            // =============================================================
            
            // 时间戳转字符串
            static std::string TimestampToString(uint64_t timestamp);
            
            // 字符串转时间戳
            static uint64_t StringToTimestamp(const std::string& timeStr);
            
            // =============================================================
            // 时间计算
            // =============================================================
            
            // 计算两个时间戳的差值（毫秒）
            static int64_t CalculateDuration(uint64_t start, uint64_t end);
            
            // 计算两个时间戳的差值（秒）
            static int64_t CalculateDurationSeconds(uint64_t start, uint64_t end);
            
            // =============================================================
            // 过期检查
            // =============================================================
            
            // 检查是否过期（毫秒）
            static bool IsExpired(uint64_t timestamp, uint64_t durationMs);
            
            // 检查是否过期（秒）
            static bool IsExpiredSeconds(uint64_t timestamp, uint64_t durationSeconds);
            
            // 检查是否过期（天）
            static bool IsExpiredDays(uint64_t timestamp, uint64_t days);
            
            // =============================================================
            // 格式转换
            // =============================================================
            
            // 获取当前时间字符串
            static std::string GetCurrentTimeString();
            
            // 获取当前日期字符串
            static std::string GetCurrentDateString();
            
            // 获取当前日期时间字符串
            static std::string GetCurrentDateTimeString();
            
        private:
            // 禁止实例化
            Time() = delete;
            ~Time() = delete;
        };
        
    } // namespace common
} // namespace ZChatIM
