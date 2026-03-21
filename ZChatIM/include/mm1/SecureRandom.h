#pragma once

#include "../Types.h"
#include <vector>

namespace ZChatIM
{
    namespace mm1
    {
        namespace security
        {
            // =============================================================
            // 安全随机数
            // =============================================================
            
            class SecureRandom {
            public:
                // =============================================================
                // 构造函数/析构函数
                // =============================================================
                
                SecureRandom();
                ~SecureRandom();
                
                // =============================================================
                // 随机数生成
                // =============================================================
                
                // 生成加密安全的随机字节
                std::vector<uint8_t> Generate(size_t size);
                
                // 生成随机字节（直接写入缓冲区）
                bool Generate(uint8_t* buffer, size_t size);
                
                // =============================================================
                // 随机数类型
                // =============================================================
                
                // 生成随机整数
                int32_t GenerateInt(int32_t min, int32_t max);
                
                // 生成随机无符号整数
                uint32_t GenerateUInt(uint32_t min, uint32_t max);
                
                // 生成随机64位整数
                int64_t GenerateInt64(int64_t min, int64_t max);
                
                // 生成随机无符号64位整数
                uint64_t GenerateUInt64(uint64_t min, uint64_t max);
                
                // 生成随机布尔值
                bool GenerateBool();
                
                // =============================================================
                // 特定用途随机数
                // =============================================================
                
                // 生成消息ID
                std::vector<uint8_t> GenerateMessageId();
                
                // 生成会话ID
                std::vector<uint8_t> GenerateSessionId();
                
                // 生成文件ID
                std::string GenerateFileId();
                
                // 生成随机字符串
                std::string GenerateRandomString(size_t length);
                
                // =============================================================
                // 随机数质量
                // =============================================================
                
                // 检查随机数质量
                bool CheckQuality();
                
                // 获取随机数熵
                double GetEntropy();
                
                // =============================================================
                // 初始化
                // =============================================================
                
                // 初始化随机数生成器
                bool Initialize();
                
                // 清理随机数生成器
                void Cleanup();
                
                // 检查是否初始化
                bool IsInitialized();
                
            private:
                // 禁止实例化
                SecureRandom(const SecureRandom&) = delete;
                SecureRandom& operator=(const SecureRandom&) = delete;
                
                // 内部方法
                bool InitSystemRandom();
                bool InitHardwareRandom();
                
                // 成员变量
                bool m_initialized;
            };
            
        } // namespace security
    } // namespace mm1
} // namespace ZChatIM
