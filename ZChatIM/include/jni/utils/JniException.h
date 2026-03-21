#pragma once

#include "../Types.h"
#include <string>
#include <stdexcept>

namespace ZChatIM
{
    namespace jni
    {
        // =============================================================
        // JNI 异常处理（暂时移除 JNI 依赖）
        // =============================================================
        
        class JniException : public std::runtime_error {
        public:
            // 错误码枚举
            enum class ErrorCode {
                SUCCESS = 0,
                JNI_ENV_ERROR = 1,
                JNI_CLASS_ERROR = 2,
                JNI_METHOD_ERROR = 3,
                JNI_MEMORY_ERROR = 4,
                JNI_TYPE_ERROR = 5,
                SECURITY_ERROR = 6,
                STORAGE_ERROR = 7,
                NETWORK_ERROR = 8,
                INVALID_PARAMETER = 9,
                UNKNOWN_ERROR = 10
            };
            
            // 构造函数
            JniException(ErrorCode code, const std::string& message);
            
            // 获取错误码
            ErrorCode GetErrorCode() const;
            
            // 获取错误消息
            std::string GetErrorMessage() const;
            
            // 错误码转字符串
            static std::string ErrorCodeToString(ErrorCode code);
            
        private:
            ErrorCode m_errorCode;
            std::string m_errorMessage;
        };
        
        // =============================================================
        // 异常工具函数
        // =============================================================
        
        class ExceptionUtils {
        public:
            // 抛出异常
            static void ThrowException(JniException::ErrorCode code, const std::string& message);
            
            // 捕获并处理异常
            static std::string CatchException();
            
            // 检查参数有效性
            static void CheckParameter(bool condition, const std::string& message);
            
            // 检查内存分配
            static void CheckMemory(void* ptr, const std::string& message);
            
            // 检查操作结果
            static void CheckResult(bool result, JniException::ErrorCode code, const std::string& message);
            
        private:
            // 禁止实例化
            ExceptionUtils() = delete;
            ~ExceptionUtils() = delete;
        };
        
    } // namespace jni
} // namespace ZChatIM
