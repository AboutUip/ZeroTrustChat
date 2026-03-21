#pragma once

#include <string>
#include <cstdarg>

namespace ZChatIM
{
    // =============================================================
    // 日志级别
    // =============================================================
    
    enum class LogLevel {
        DEBUG = 0,    // 调试信息
        INFO = 1,     // 普通信息
        WARN = 2,     // 警告信息
        ERROR = 3,    // 错误信息
        FATAL = 4,    // 致命错误
    };
    
    // =============================================================
    // 日志类
    // =============================================================
    
    class Logger {
    public:
        // 获取单例实例
        static Logger& Instance() {
            static Logger instance;
            return instance;
        }
        
        // 设置日志级别
        void SetLogLevel(LogLevel level);
        
        // 日志输出方法
        void Debug(const char* format, ...);
        void Info(const char* format, ...);
        void Warn(const char* format, ...);
        void Error(const char* format, ...);
        void Fatal(const char* format, ...);
        
        // 设置日志文件 (可选)
        bool SetLogFile(const std::string& filePath);
        
        // 关闭日志文件
        void CloseLogFile();
        
    private:
        Logger();
        ~Logger();
        
        // 禁止复制和赋值
        Logger(const Logger&) = delete;
        Logger& operator=(const Logger&) = delete;
        
        // 内部日志输出
        void Log(LogLevel level, const char* format, va_list args);
        
        // 成员变量
        LogLevel m_logLevel;    // 当前日志级别
        FILE* m_logFile;        // 日志文件句柄
    };
    
    // =============================================================
    // 日志宏定义
    // =============================================================
    
    #define LOG_DEBUG(fmt, ...) ZChatIM::Logger::Instance().Debug(fmt, ##__VA_ARGS__)
    #define LOG_INFO(fmt, ...) ZChatIM::Logger::Instance().Info(fmt, ##__VA_ARGS__)
    #define LOG_WARN(fmt, ...) ZChatIM::Logger::Instance().Warn(fmt, ##__VA_ARGS__)
    #define LOG_ERROR(fmt, ...) ZChatIM::Logger::Instance().Error(fmt, ##__VA_ARGS__)
    #define LOG_FATAL(fmt, ...) ZChatIM::Logger::Instance().Fatal(fmt, ##__VA_ARGS__)
    
} // namespace ZChatIM
