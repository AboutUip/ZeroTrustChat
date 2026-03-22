#pragma once

#include "../Types.h"
#include <string>
#include <vector>

namespace ZChatIM
{
    namespace mm1
    {
        namespace security
        {
            // =============================================================
            // JNI 安全
            // =============================================================
            
            class JniSecurity {
            public:
                // =============================================================
                // 构造函数/析构函数
                // =============================================================
                
                JniSecurity();
                ~JniSecurity();
                
                // =============================================================
                // JNI 调用验证
                // =============================================================
                
                // 验证 JNI 调用
                bool ValidateCall(const void* jniEnv, const void* jclass);
                
                // 验证 JNI 环境
                bool ValidateEnvironment(const void* jniEnv);
                
                // 验证 JNI 类
                bool ValidateClass(const void* jniEnv, const void* jclass);
                
                // =============================================================
                // JNI 类型转换
                // =============================================================
                
                // 安全的 JNI 字符串转换
                std::string StringFromJni(const void* jniEnv, const void* jstring);
                
                // 安全的 JNI 字节数组转换
                std::vector<uint8_t> ByteArrayFromJni(const void* jniEnv, const void* jbyteArray);
                
                // 安全的 JNI 整数转换
                int32_t IntFromJni(const void* jniEnv, const void* jvalue);
                
                // 安全的 JNI 长整数转换
                int64_t LongFromJni(const void* jniEnv, const void* jvalue);
                
                // =============================================================
                // JNI 内存管理
                // =============================================================
                // **`jniEnv`**：预留与 JNI 分配策略对齐；**当前实现**委托 **`common::Memory::Allocate` / `Free`**（**`Allocate` 成功**后 **`SecureZero`** 整块），与 **`02-Cpp-Completion-Roadmap.md` M0** 一致。
                
                // 安全的 JNI 侧辅助缓冲区分配（**非** `NewDirectByteBuffer` 等 JVM 堆对象）
                void* AllocateJniMemory(const void* jniEnv, size_t size);
                
                void FreeJniMemory(const void* jniEnv, void* ptr);
                
                // =============================================================
                // JNI 异常处理
                // =============================================================
                
                // 检查 JNI 异常
                bool CheckException(const void* jniEnv);
                
                // 清除 JNI 异常
                void ClearException(const void* jniEnv);
                
                // 处理 JNI 异常
                bool HandleException(const void* jniEnv);
                
                // =============================================================
                // JNI 安全配置
                // =============================================================
                
                // 启用 JNI 安全检查
                void EnableSecurityChecks();
                
                // 禁用 JNI 安全检查
                void DisableSecurityChecks();
                
                // 检查 JNI 安全检查状态
                bool IsSecurityChecksEnabled();
                
            private:
                // 禁止实例化
                JniSecurity(const JniSecurity&) = delete;
                JniSecurity& operator=(const JniSecurity&) = delete;
                
                // 内部方法
                bool IsValidJniEnv(const void* jniEnv);
                bool IsValidJniClass(const void* jniEnv, const void* jclass);
                
                // 成员变量
                bool m_securityChecksEnabled;
            };
            
        } // namespace security
    } // namespace mm1
} // namespace ZChatIM
