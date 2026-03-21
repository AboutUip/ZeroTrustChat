// ZChatIM 进程内日志（stderr + 可选文件）；与 `include/Logger.h` 宏 `LOG_*` 对应。
#include "Logger.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <string>

namespace ZChatIM {

    namespace {

        std::mutex g_logMutex;

        const char* LevelTag(LogLevel level)
        {
            switch (level) {
                case LogLevel::DEBUG:
                    return "DEBUG";
                case LogLevel::INFO:
                    return "INFO";
                case LogLevel::WARN:
                    return "WARN";
                case LogLevel::ERROR:
                    return "ERROR";
                case LogLevel::FATAL:
                    return "FATAL";
                default:
                    return "LOG";
            }
        }

        std::string CurrentTimePrefix()
        {
            using clock = std::chrono::system_clock;
            const std::time_t t = clock::to_time_t(clock::now());
            std::tm tmBuf{};
#if defined(_WIN32)
            gmtime_s(&tmBuf, &t);
#else
            gmtime_r(&t, &tmBuf);
#endif
            char buf[32];
            std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmBuf);
            return std::string(buf);
        }

    } // namespace

    Logger::Logger()
        : m_logFile(nullptr)
    {
        m_logLevel.store(LogLevel::INFO, std::memory_order_relaxed);
    }

    Logger::~Logger()
    {
        std::lock_guard<std::mutex> lock(g_logMutex);
        if (m_logFile) {
            std::fclose(m_logFile);
            m_logFile = nullptr;
        }
    }

    void Logger::SetLogLevel(LogLevel level)
    {
        m_logLevel.store(level, std::memory_order_relaxed);
    }

    void Logger::Log(LogLevel level, const char* format, va_list args)
    {
        if (level < m_logLevel.load(std::memory_order_relaxed))
            return;
        std::lock_guard<std::mutex> lock(g_logMutex);
        std::string prefix = CurrentTimePrefix();
        prefix += " [";
        prefix += LevelTag(level);
        prefix += "] ";

        if (!format) {
            std::fputs(prefix.c_str(), stderr);
            std::fputs("(null format)\n", stderr);
            std::fflush(stderr);
            if (m_logFile) {
                std::fputs(prefix.c_str(), m_logFile);
                std::fputs("(null format)\n", m_logFile);
                std::fflush(m_logFile);
            }
            return;
        }

        va_list aStderr;
        va_copy(aStderr, args);
        std::fputs(prefix.c_str(), stderr);
        std::vfprintf(stderr, format, aStderr);
        va_end(aStderr);
        std::fputc('\n', stderr);
        std::fflush(stderr);

        if (m_logFile) {
            va_list aFile;
            va_copy(aFile, args);
            std::fputs(prefix.c_str(), m_logFile);
            std::vfprintf(m_logFile, format, aFile);
            va_end(aFile);
            std::fputc('\n', m_logFile);
            std::fflush(m_logFile);
        }
    }

    void Logger::Debug(const char* format, ...)
    {
        va_list ap;
        va_start(ap, format);
        Log(LogLevel::DEBUG, format, ap);
        va_end(ap);
    }

    void Logger::Info(const char* format, ...)
    {
        va_list ap;
        va_start(ap, format);
        Log(LogLevel::INFO, format, ap);
        va_end(ap);
    }

    void Logger::Warn(const char* format, ...)
    {
        va_list ap;
        va_start(ap, format);
        Log(LogLevel::WARN, format, ap);
        va_end(ap);
    }

    void Logger::Error(const char* format, ...)
    {
        va_list ap;
        va_start(ap, format);
        Log(LogLevel::ERROR, format, ap);
        va_end(ap);
    }

    void Logger::Fatal(const char* format, ...)
    {
        va_list ap;
        va_start(ap, format);
        Log(LogLevel::FATAL, format, ap);
        va_end(ap);
    }

    bool Logger::SetLogFile(const std::string& filePath)
    {
        std::lock_guard<std::mutex> lock(g_logMutex);
        if (m_logFile) {
            std::fclose(m_logFile);
            m_logFile = nullptr;
        }
        m_logFile = std::fopen(filePath.c_str(), "a");
        return m_logFile != nullptr;
    }

    void Logger::CloseLogFile()
    {
        std::lock_guard<std::mutex> lock(g_logMutex);
        if (m_logFile) {
            std::fclose(m_logFile);
            m_logFile = nullptr;
        }
    }

} // namespace ZChatIM
