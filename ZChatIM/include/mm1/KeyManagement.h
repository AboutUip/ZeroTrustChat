#pragma once

#include "../Types.h"
#include <vector>
#include <string>

namespace ZChatIM
{
    namespace mm1
    {
        namespace security
        {
            // =============================================================
            // 密钥管理
            // =============================================================
            
            class KeyManagement {
            public:
                // =============================================================
                // 构造函数/析构函数
                // =============================================================
                
                KeyManagement();
                ~KeyManagement();
                
                // =============================================================
                // 密钥生成
                // =============================================================
                
                // 生成主密钥
                std::vector<uint8_t> GenerateMasterKey();
                
                // 生成会话密钥
                std::vector<uint8_t> GenerateSessionKey();
                
                // 生成消息密钥
                std::vector<uint8_t> GenerateMessageKey();
                
                // 生成随机密钥
                std::vector<uint8_t> GenerateRandomKey(size_t keySize);
                
                // =============================================================
                // 密钥派生
                // =============================================================
                
                // 派生密钥
                std::vector<uint8_t> DeriveKey(const std::vector<uint8_t>& inputKey, const std::vector<uint8_t>& salt);
                
                // 派生密钥（指定长度）
                std::vector<uint8_t> DeriveKey(const std::vector<uint8_t>& inputKey, const std::vector<uint8_t>& salt, size_t outputKeySize);
                
                // =============================================================
                // 密钥存储
                // =============================================================
                
                // 存储主密钥
                bool StoreMasterKey(const std::vector<uint8_t>& key);
                
                // 获取主密钥
                std::vector<uint8_t> GetMasterKey();
                
                // 清除主密钥
                void ClearMasterKey();
                
                // =============================================================
                // 密钥刷新
                // =============================================================
                
                // 刷新主密钥
                std::vector<uint8_t> RefreshMasterKey();
                
                // 刷新会话密钥
                std::vector<uint8_t> RefreshSessionKey();
                
                // =============================================================
                // 密钥验证
                // =============================================================
                
                // 验证密钥
                bool ValidateKey(const std::vector<uint8_t>& key);
                
                // 检查密钥强度
                bool CheckKeyStrength(const std::vector<uint8_t>& key);
                
                // =============================================================
                // 密钥导出
                // =============================================================
                
                // 导出密钥
                bool ExportKey(const std::vector<uint8_t>& key, const std::string& filePath, const std::string& password);
                
                // 导入密钥
                std::vector<uint8_t> ImportKey(const std::string& filePath, const std::string& password);
                
            private:
                // 禁止实例化
                KeyManagement(const KeyManagement&) = delete;
                KeyManagement& operator=(const KeyManagement&) = delete;
                
                // 内部方法
                bool IsValidKeySize(size_t keySize);
                bool IsStrongKey(const std::vector<uint8_t>& key);
                
                // 成员变量
                std::vector<uint8_t> m_masterKey;
            };
            
        } // namespace security
    } // namespace mm1
} // namespace ZChatIM
