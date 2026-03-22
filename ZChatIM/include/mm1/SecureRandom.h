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
                
                // 生成加密安全随机字节（→ **`common::Random::GenerateSecureBytes`**；**RAND 失败** 返回**空向量**）
                std::vector<uint8_t> Generate(size_t size);
                
                // 写入缓冲区（→ **`GenerateSecureBytes`**；**size 不符** 时 **`false`**）
                bool Generate(uint8_t* buffer, size_t size);
                
                // =============================================================
                // 随机数类型
                // =============================================================
                
                // 生成随机整数（委托 **`common::Random::GenerateSecureInt`**；**RAND 失败**时返回 **`min`**）
                int32_t GenerateInt(int32_t min, int32_t max);
                
                // 生成随机无符号整数（同上 **`GenerateSecureUInt`**）
                uint32_t GenerateUInt(uint32_t min, uint32_t max);
                
                // 生成随机 64 位整数：**`mt19937_64`** 输出（**进程内单例**、**一次性**以 **`mm2::Crypto::GenerateSecureRandom(8)`** 播种）；**非**每调用 **RAND_bytes**。密钥材料请用 **`KeyManagement`** / **`mm2::Crypto::GenerateSecureRandom`**。
                int64_t GenerateInt64(int64_t min, int64_t max);
                
                // 生成随机无符号 64 位整数（同上 **`mt19937_64`** 语义）
                uint64_t GenerateUInt64(uint64_t min, uint64_t max);
                
                // 生成随机布尔值（**1 字节** **`GenerateSecureBytes`**；**空** 时 **`false`**）
                bool GenerateBool();
                
                // =============================================================
                // 特定用途随机数
                // =============================================================
                
                // 生成消息 ID（16B，→ **`common::Random::GenerateMessageId`**；失败**空向量**）
                std::vector<uint8_t> GenerateMessageId();
                
                // 生成会话 ID（4B，→ **`GenerateSessionId`**；失败**空向量**）
                std::vector<uint8_t> GenerateSessionId();
                
                // 生成文件 ID（8 字符；→ **`GenerateFileId`**；**RAND 失败** **空串**）
                std::string GenerateFileId();
                
                // 随机字符串（→ **`GenerateRandomString`**；**超限或 RAND 失败** **空串**）
                std::string GenerateRandomString(size_t length);
                
                // =============================================================
                // 随机数质量
                // =============================================================
                
                // 能否成功读出 8 字节 CSPRNG（→ **`GenerateSecureBytes(8)`**，**`size==8`**）
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
