#pragma once

#include "Types.h"
#include <vector>
#include <string>

namespace ZChatIM
{
    namespace mm2
    {
        // =============================================================
        // 加密组件
        // =============================================================
        
        class Crypto {
        public:
            // =============================================================
            // 初始化
            // =============================================================
            // **平台后端**：**Windows** — **BCrypt**（AES-GCM / PBKDF2 / RNG 链 + **CryptoAPI** 随机后备）。**Linux / macOS** — **OpenSSL 3 libcrypto**（**`EVP_aes_256_gcm`** / **`PKCS5_PBKDF2_HMAC`** / **`RAND_bytes`** + **`/dev/urandom`** 后备）。须与 **`MM2::Cleanup` / `CleanupUnlocked`** 中 **`Cleanup()`** 成对调用（由 MM2 编排）。
            // **磁盘格式**：**nonce(12) ‖ ciphertext ‖ tag(16)**，**Windows 与 Unix 字节级一致**（算法同为 AES-256-GCM）。
            // **构建**：**Windows** 链接 **bcrypt、advapi32**；**非 Windows** **`find_package(OpenSSL 3.0)`** + **`OpenSSL::Crypto`**。
            // **调用约定**：**`EncryptMessage` / `DecryptMessage` / `DeriveKey`** 要求 **`Init()` 已成功**（**`s_initialized`**）；**`GenerateSecureRandom` / `HashSha256`** **不**检查该标志（**`ZdbFile::Create`**、**`StoreFriendRequest`** 等可在 **`Init`** 前取随机；**`HashSha256`** 为便携 **`crypto::Sha256`**）。
            
            // 初始化加密库
            static bool Init();
            
            // 清理加密库
            static void Cleanup();
            
            // =============================================================
            // 消息加密
            // =============================================================
            
            // 加密消息内容
            static bool EncryptMessage(
                const uint8_t* plaintext, size_t plaintextLen,
                const uint8_t* key, size_t keyLen,
                uint8_t* nonce, size_t nonceLen,
                std::vector<uint8_t>& ciphertext,
                uint8_t* authTag, size_t authTagLen
            );
            
            // 解密消息内容
            static bool DecryptMessage(
                const uint8_t* ciphertext, size_t ciphertextLen,
                const uint8_t* key, size_t keyLen,
                const uint8_t* nonce, size_t nonceLen,
                const uint8_t* authTag, size_t authTagLen,
                std::vector<uint8_t>& plaintext
            );
            
            // =============================================================
            // 密钥管理
            // =============================================================
            
            // 生成随机密钥
            static std::vector<uint8_t> GenerateKey(size_t keyLen = ZChatIM::CRYPTO_KEY_SIZE);
            
            // 生成随机Nonce
            static std::vector<uint8_t> GenerateNonce(size_t nonceLen = ZChatIM::NONCE_SIZE);
            
            // 派生密钥
            static bool DeriveKey(
                const uint8_t* inputKey, size_t inputKeyLen,
                const uint8_t* salt, size_t saltLen,
                uint8_t* outputKey, size_t outputKeyLen
            );
            
            // =============================================================
            // 哈希函数
            // =============================================================
            
            // SHA-256哈希
            static bool HashSha256(const uint8_t* data, size_t dataLen, uint8_t* hash);
            
            // SHA-256哈希（返回向量）
            static std::vector<uint8_t> HashSha256(const uint8_t* data, size_t dataLen);
            
            // 计算消息ID哈希
            static bool CalculateMessageIdHash(const uint8_t* messageId, size_t messageIdLen, uint8_t* hash);
            
            // =============================================================
            // 安全随机数
            // =============================================================
            
            // 生成加密安全的随机字节
            static std::vector<uint8_t> GenerateSecureRandom(size_t length);
            
        private:
            // 禁止实例化
            Crypto() = delete;
            ~Crypto() = delete;
            
            // 初始化状态
            static bool s_initialized;
        };
        
    } // namespace mm2
} // namespace ZChatIM
