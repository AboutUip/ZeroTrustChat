#pragma once

#include <vector>
#include <string>
#include <cstdint>

namespace ZChatIM
{
    namespace common
    {
        // =============================================================
        // 随机数生成器
        // =============================================================
        
        class Random {
        public:
            // =============================================================
            // 普通随机数
            // =============================================================
            
            // 生成指定长度的随机字节
            static std::vector<uint8_t> GenerateBytes(size_t length);
            
            // 生成指定范围内的随机整数
            static int32_t GenerateInt(int32_t min, int32_t max);
            
            // 生成指定范围内的随机无符号整数
            static uint32_t GenerateUInt(uint32_t min, uint32_t max);
            
            // 生成随机布尔值
            static bool GenerateBool();
            
            // =============================================================
            // 加密安全随机数
            // =============================================================
            
            // 生成加密安全的随机字节
            static std::vector<uint8_t> GenerateSecureBytes(size_t length);
            
            // 生成加密安全的随机整数
            static int32_t GenerateSecureInt(int32_t min, int32_t max);
            
            // 生成加密安全的随机无符号整数
            static uint32_t GenerateSecureUInt(uint32_t min, uint32_t max);
            
            // =============================================================
            // 特定用途随机数
            // =============================================================
            
            // 生成消息ID (16字节)
            static std::vector<uint8_t> GenerateMessageId();
            
            // 生成会话ID (4字节)
            static std::vector<uint8_t> GenerateSessionId();
            
            // 生成文件ID (8字符)
            static std::string GenerateFileId();
            
            // 生成随机字符串
            static std::string GenerateRandomString(size_t length);
            
        private:
            // 禁止实例化
            Random() = delete;
            ~Random() = delete;
            
            // 初始化随机数生成器
            static void Init();
            
            static bool s_initialized; // 初始化标志
        };
        
    } // namespace common
} // namespace ZChatIM
