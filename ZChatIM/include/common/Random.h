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
        //
        // **线程安全**：全部静态 API 可多线程并发调用。OS CSPRNG（OpenSSL **RAND_bytes**；Unix 可再读 **/dev/urandom**）路径带内部同步。
        // **`GenerateBytes` / `GenerateInt*` / `GenerateBool`** 使用 **`mt19937`**（**非** CSPRNG，互斥保护）；**`GenerateSecure*`**、**`GenerateMessageId`**、**`GenerateSessionId`**、**`GenerateFileId`**、**`GenerateRandomString`** 仅依赖 **RAND**（失败时返回空向量/空串或 **`GenerateSecureInt*` 返回 `min`**），**不**回退到 **`mt19937`**。
        //
        class Random {
        public:
            // =============================================================
            // 普通随机数
            // =============================================================
            
            // 生成指定长度的随机字节（**`length` 超过实现上限** 时返回**空向量**；见 `Random.cpp` 中 **`kMaxRandomByteBlob`**）
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
            
            // 生成加密安全的随机字节（**`length` 上限**同 **`GenerateBytes`**；**RAND 失败**时返回**空向量**）
            static std::vector<uint8_t> GenerateSecureBytes(size_t length);
            
            // 生成加密安全的随机整数（**RAND 失败**时返回 **`min`**）
            static int32_t GenerateSecureInt(int32_t min, int32_t max);
            
            // 生成加密安全的随机无符号整数（**RAND 失败**时返回 **`min`**）
            static uint32_t GenerateSecureUInt(uint32_t min, uint32_t max);
            
            // =============================================================
            // 特定用途随机数
            // =============================================================
            
            // 生成消息ID (16字节)
            static std::vector<uint8_t> GenerateMessageId();
            
            // 生成会话ID (4字节)
            static std::vector<uint8_t> GenerateSessionId();
            
            // 生成文件 ID（8 字符）；字母表上 **均匀分布**（**仅** OS CSPRNG + 拒绝采样；**失败**时返回**空串**）
            static std::string GenerateFileId();
            
            // 随机字符串（同上，**仅** CSPRNG、**均匀** 62 字母表；**`length` 超限** 或 **RAND 失败** 时返回**空串**）
            static std::string GenerateRandomString(size_t length);
            
        private:
            // 禁止实例化
            Random() = delete;
            ~Random() = delete;
            
            // 初始化随机数生成器（惰性；多线程安全）
            static void Init();
        };
        
    } // namespace common
} // namespace ZChatIM
