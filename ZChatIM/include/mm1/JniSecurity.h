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
            class JniSecurity {
            public:
                JniSecurity();
                ~JniSecurity();

                bool ValidateCall(const void* jniEnv, const void* jcls);

                bool ValidateEnvironment(const void* jniEnv);

                bool ValidateClass(const void* jniEnv, const void* jcls);

                std::string StringFromJni(const void* jniEnv, const void* jstr);

                std::vector<uint8_t> ByteArrayFromJni(const void* jniEnv, const void* jbytes);

                int32_t IntFromJni(const void* jniEnv, const void* jobj);

                int64_t LongFromJni(const void* jniEnv, const void* jobj);

                // common::Memory::Allocate/Free; SecureZero on success.
                void* AllocateJniMemory(const void* jniEnv, size_t size);

                void FreeJniMemory(const void* jniEnv, void* ptr);

                bool CheckException(const void* jniEnv);

                void ClearException(const void* jniEnv);

                bool HandleException(const void* jniEnv);

                void EnableSecurityChecks();

                void DisableSecurityChecks();

                bool IsSecurityChecksEnabled();

            private:
                JniSecurity(const JniSecurity&) = delete;
                JniSecurity& operator=(const JniSecurity&) = delete;

                bool IsValidJniEnv(const void* jniEnv);
                bool IsValidJniClass(const void* jniEnv, const void* jcls);

                bool m_securityChecksEnabled;
            };

        } // namespace security
    } // namespace mm1
} // namespace ZChatIM
