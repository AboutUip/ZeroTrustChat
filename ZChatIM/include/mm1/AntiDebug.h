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
            // 反调试
            // =============================================================
            
            class AntiDebug {
            public:
                // =============================================================
                // 构造函数/析构函数
                // =============================================================
                
                AntiDebug();
                ~AntiDebug();
                
                // =============================================================
                // 调试器检测
                // =============================================================
                
                // 检测调试器
                bool IsDebuggerPresent();
                
                // 检测硬件断点
                bool IsHardwareBreakpointPresent();
                
                // 检测软件断点
                bool IsSoftwareBreakpointPresent();
                
                // =============================================================
                // 反调试保护
                // =============================================================
                
                // 启用反调试保护
                bool Enable();
                
                // 禁用反调试保护
                void Disable();
                
                // 检查反调试状态
                bool IsEnabled();
                
                // =============================================================
                // 反调试技术
                // =============================================================
                
                // 时间检测
                bool DetectTimeBreakpoint();
                
                // 内存检测
                bool DetectMemoryBreakpoint();
                
                // 线程检测
                bool DetectThreadBreakpoint();
                
                // 异常检测
                bool DetectExceptionBreakpoint();
                
                // =============================================================
                // 自我保护
                // =============================================================
                
                // 保护代码段
                bool ProtectCodeSection();
                
                // 保护数据段
                bool ProtectDataSection();
                
                // 混淆代码
                bool ObfuscateCode();
                
            private:
                // 禁止实例化
                AntiDebug(const AntiDebug&) = delete;
                AntiDebug& operator=(const AntiDebug&) = delete;
                
                // 内部方法
                bool CheckPEB();
                bool CheckProcessHeap();
                bool CheckThreadEnvironmentBlock();
                bool CheckDebugPort();
                
                // 成员变量
                bool m_enabled;
            };
            
        } // namespace security
    } // namespace mm1
} // namespace ZChatIM
