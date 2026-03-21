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
            // 内存加密
            // =============================================================
            
            class MemoryEncryption {
            public:
                // =============================================================
                // 构造函数/析构函数
                // =============================================================
                
                MemoryEncryption();
                ~MemoryEncryption();
                
                // =============================================================
                // 内存加密/解密
                // =============================================================
                
                // 加密内存
                bool Encrypt(void* ptr, size_t size, const uint8_t* key, size_t keySize);
                
                // 解密内存
                bool Decrypt(void* ptr, size_t size, const uint8_t* key, size_t keySize);
                
                // =============================================================
                // 块加密
                // =============================================================
                
                // 加密内存块
                bool EncryptBlock(void* ptr, size_t size, const uint8_t* key);
                
                // 解密内存块
                bool DecryptBlock(void* ptr, size_t size, const uint8_t* key);
                
                // =============================================================
                // 密钥管理
                // =============================================================
                
                // 生成加密密钥
                bool GenerateKey(uint8_t* key, size_t keySize);
                
                // 派生密钥
                bool DeriveKey(const uint8_t* inputKey, size_t inputKeySize, uint8_t* outputKey, size_t outputKeySize);
                
                // =============================================================
                // 辅助方法
                // =============================================================
                
                // 检查密钥大小
                bool IsValidKeySize(size_t keySize);
                
                // 获取推荐密钥大小
                size_t GetRecommendedKeySize();
                
            private:
                // 禁止实例化
                MemoryEncryption(const MemoryEncryption&) = delete;
                MemoryEncryption& operator=(const MemoryEncryption&) = delete;
                
                // 内部方法
                bool EncryptXor(void* ptr, size_t size, const uint8_t* key, size_t keySize);
                bool DecryptXor(void* ptr, size_t size, const uint8_t* key, size_t keySize);
            };
            
        } // namespace security
    } // namespace mm1
} // namespace ZChatIM
