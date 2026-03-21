#pragma once

#include "../Types.h"
#include <string>
#include <vector>

namespace ZChatIM
{
    namespace jni
    {
        // =============================================================
        // JNI 工具函数（暂时移除 JNI 依赖）
        // =============================================================
        
        class JniUtils {
        public:
            // =============================================================
            // 类型转换
            // =============================================================
            
            // 字节数组转字符串
            static std::string ByteArrayToString(const std::vector<uint8_t>& bytes);
            
            // 字符串转字节数组
            static std::vector<uint8_t> StringToByteArray(const std::string& str);
            
            // 数值转字节数组
            static std::vector<uint8_t> IntToByteArray(int value);
            static std::vector<uint8_t> LongToByteArray(int64_t value);
            
            // 字节数组转数值
            static int ByteArrayToInt(const std::vector<uint8_t>& bytes);
            static int64_t ByteArrayToLong(const std::vector<uint8_t>& bytes);
            
            // =============================================================
            // 数组操作
            // =============================================================
            
            // 合并字节数组
            static std::vector<uint8_t> CombineByteArrays(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b);
            
            // 分割字节数组
            static std::vector<std::vector<uint8_t>> SplitByteArray(const std::vector<uint8_t>& bytes, size_t chunkSize);
            
            // 复制字节数组
            static std::vector<uint8_t> CopyByteArray(const std::vector<uint8_t>& bytes);
            
            // =============================================================
            // 字符串操作
            // =============================================================
            
            // 生成随机字符串
            static std::string GenerateRandomString(size_t length);
            
            // 生成文件ID
            static std::string GenerateFileId();
            
            // 生成消息ID
            static std::vector<uint8_t> GenerateMessageId();
            
            // =============================================================
            // 时间操作
            // =============================================================
            
            // 获取当前时间戳（毫秒）
            static int64_t GetCurrentTimestamp();
            
            // 时间戳转字符串
            static std::string TimestampToString(int64_t timestamp);
            
            // =============================================================
            // 错误处理
            // =============================================================
            
            // 格式化错误消息
            static std::string FormatErrorMessage(const std::string& message, int errorCode);
            
            // 记录错误
            static void LogError(const std::string& message);
            
            // 记录警告
            static void LogWarning(const std::string& message);
            
            // 记录信息
            static void LogInfo(const std::string& message);
            
            // =============================================================
            // 安全操作
            // =============================================================
            
            // 安全比较两个字节数组
            static bool SecureCompare(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b);
            
            // 安全清理字节数组
            static void SecureClear(std::vector<uint8_t>& bytes);
            
            // 验证数据长度
            static bool ValidateDataLength(const std::vector<uint8_t>& data, size_t minLength, size_t maxLength);
            
            // 验证字符串格式
            static bool ValidateStringFormat(const std::string& str, const std::string& pattern);
            
        private:
            // 禁止实例化
            JniUtils() = delete;
            ~JniUtils() = delete;
        };
        
    } // namespace jni
} // namespace ZChatIM
