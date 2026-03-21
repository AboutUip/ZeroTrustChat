#pragma once

#include "Types.h"
#include <vector>
#include <string>

namespace ZChatIM
{
    namespace mm2
    {
        // =============================================================
        // 消息块
        // =============================================================
        
        class MessageBlock {
        public:
            // =============================================================
            // 构造函数
            // =============================================================
            
            MessageBlock();
            MessageBlock(const std::vector<uint8_t>& messageId, const std::vector<uint8_t>& payload);
            
            // =============================================================
            // 消息块操作
            // =============================================================
            
            // 构造消息块
            bool Construct(
                const std::vector<uint8_t>& messageId,
                const std::vector<uint8_t>& payload,
                const std::vector<uint8_t>& senderId,
                uint64_t sequence,
                const std::vector<uint8_t>& prevHash
            );
            
            // 序列化消息块
            bool Serialize(std::vector<uint8_t>& output);
            
            // 反序列化消息块
            bool Deserialize(const std::vector<uint8_t>& input);
            
            // 验证消息块
            bool Validate() const;
            
            // =============================================================
            // 访问器
            // =============================================================
            
            // 获取消息ID
            std::vector<uint8_t> GetMessageId() const;
            
            // 获取消息内容
            std::vector<uint8_t> GetPayload() const;
            
            // 获取发送者ID
            std::vector<uint8_t> GetSenderId() const;
            
            // 获取序列号
            uint64_t GetSequence() const;
            
            // 获取时间戳
            uint64_t GetTimestamp() const;
            
            // 获取前一条消息哈希
            std::vector<uint8_t> GetPrevHash() const;
            
            // 获取消息块大小
            size_t GetSize() const;
            
            // =============================================================
            // 静态方法
            // =============================================================
            
            // 计算消息块大小
            static size_t CalculateSize(size_t payloadSize);
            
            // 验证消息块头部
            static bool ValidateHeader(const BlockHead& head);
            
        private:
            // =============================================================
            // 内部方法
            // =============================================================
            
            // 生成消息ID哈希
            bool GenerateIdHash(const std::vector<uint8_t>& messageId);
            
            // 加密消息内容
            bool EncryptContent();
            
            // 解密消息内容
            bool DecryptContent();
            
            // =============================================================
            // 成员变量
            // =============================================================
            
            BlockHead m_head;                // 消息块头部
            uint8_t m_cryptoKey[CRYPTO_KEY_SIZE];  // 加密密钥
            uint8_t m_nonce[NONCE_SIZE];           // AES-GCM Nonce
            std::vector<uint8_t> m_content;        // 加密后的内容
            uint8_t m_authTag[AUTH_TAG_SIZE];      // GCM认证标签
            
            // 解密后的内容
            uint64_t m_sequence;           // 序列号
            uint64_t m_timestamp;          // 时间戳
            uint8_t m_prevHash[SHA256_SIZE]; // 前一条消息哈希
            uint8_t m_senderId[USER_ID_SIZE]; // 发送者ID
            std::vector<uint8_t> m_payload;  // 消息内容
        };
        
    } // namespace mm2
} // namespace ZChatIM
