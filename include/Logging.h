// 日志系统的外部接口
#ifndef _LOGGING_H
#define _LOGGING_H
#include "LogStream.h"
#include "AsyncLogging.h"
#include "noncopyable.h"

class Logger: public noncopyable
{
public:
    enum LogLevel
    {
        TRACE = 0,
        DEBUG,
        INFO,
        WARN,
        ERROR,
        FATAL,
        NUM_LOG_LEVELS,
    };
    Logger(const std::string basename, int line, LogLevel level = INFO);
    ~Logger();
    LogStream &stream() {return impl_.stream_;}

private:
    struct Impl
    {
        Impl(const std::string basename, int line, LogLevel level);
        ~Impl();

        LogStream stream_;
        const std::string basename_;
        int line_;
        LogLevel level_;
    };

    Impl impl_;
};

#define LOG_TRACE Logger(__FILE__, __LINE__, Logger::TRACE).stream()
#define LOG_DEBUG Logger(__FILE__, __LINE__, Logger::DEBUG).stream()
#define LOG_INFO Logger(__FILE__, __LINE__).stream()
#define LOG_WARN Logger(__FILE__, __LINE__, Logger::WARN).stream()
#define LOG_ERROR Logger(__FILE__, __LINE__, Logger::ERROR).stream()
#define LOG_FATAL Logger(__FILE__, __LINE__, Logger::FATAL).stream()

#endif