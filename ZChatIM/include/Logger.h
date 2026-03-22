#pragma once

#include <atomic>
#include <string>
#include <cstdarg>

namespace ZChatIM
{
    enum class LogLevel {
        DEBUG = 0,
        INFO = 1,
        WARN = 2,
        ERROR = 3,
        FATAL = 4,
    };

    class Logger {
    public:
        static Logger& Instance() {
            static Logger instance;
            return instance;
        }

        void SetLogLevel(LogLevel level);

        void Debug(const char* format, ...);
        void Info(const char* format, ...);
        void Warn(const char* format, ...);
        void Error(const char* format, ...);
        void Fatal(const char* format, ...);
        
        // 设置日志文件 (可选)；**`filePath` 为空** 时 **`false`**（不关 stderr，仅不打开文件）
        bool SetLogFile(const std::string& filePath);
        
        // 关闭日志文件
        void CloseLogFile();
        
    private:
        Logger();
        ~Logger();

        Logger(const Logger&) = delete;
        Logger& operator=(const Logger&) = delete;

        void Log(LogLevel level, const char* format, va_list args);

        std::atomic<LogLevel> m_logLevel;
        FILE* m_logFile;
    };

    #define LOG_DEBUG(fmt, ...) ZChatIM::Logger::Instance().Debug(fmt, ##__VA_ARGS__)
    #define LOG_INFO(fmt, ...) ZChatIM::Logger::Instance().Info(fmt, ##__VA_ARGS__)
    #define LOG_WARN(fmt, ...) ZChatIM::Logger::Instance().Warn(fmt, ##__VA_ARGS__)
    #define LOG_ERROR(fmt, ...) ZChatIM::Logger::Instance().Error(fmt, ##__VA_ARGS__)
    #define LOG_FATAL(fmt, ...) ZChatIM::Logger::Instance().Fatal(fmt, ##__VA_ARGS__)
    
} // namespace ZChatIM
