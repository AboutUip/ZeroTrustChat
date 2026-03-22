#pragma once

#include "../Types.h"
#include <cstdint>

namespace ZChatIM
{
    namespace mm1
    {
        namespace security
        {
            class AntiDebug {
            public:
                AntiDebug();
                ~AntiDebug();

                bool IsDebuggerPresent();

                bool IsHardwareBreakpointPresent();

                bool IsSoftwareBreakpointPresent();

                bool Enable();

                void Disable();

                bool IsEnabled();

                bool DetectTimeBreakpoint();

                bool DetectMemoryBreakpoint();

                bool DetectThreadBreakpoint();

                bool DetectExceptionBreakpoint();

                bool ProtectCodeSection();

                bool ProtectDataSection();

                bool ObfuscateCode();

            private:
                AntiDebug(const AntiDebug&) = delete;
                AntiDebug& operator=(const AntiDebug&) = delete;

                bool CheckPEB();
                bool CheckProcessHeap();
                bool CheckThreadEnvironmentBlock();
                bool CheckDebugPort();

                bool m_enabled;
            };

        } // namespace security
    } // namespace mm1
} // namespace ZChatIM
