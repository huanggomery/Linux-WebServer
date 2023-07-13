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
    Logger(const std::string &basename, int line, LogLevel level = INFO);
    ~Logger();
    LogStream &stream() {return impl_.stream_;}
    static LogLevel level() {return g_level_;}

private:
    struct Impl
    {
        Impl(const std::string &basename, int line, LogLevel level);
        ~Impl();

        LogStream stream_;
        const std::string basename_;
        int line_;
        LogLevel level_;
    };

    static LogLevel g_level_;
    Impl impl_;
};

#define LOG_TRACE if (Logger::level() <= Logger::TRACE) \
    Logger(__FILE__, __LINE__, Logger::TRACE).stream()
#define LOG_DEBUG if (Logger::level() <= Logger::DEBUG) \
    Logger(__FILE__, __LINE__, Logger::DEBUG).stream()
#define LOG_INFO if (Logger::level() <= Logger::INFO) \
    Logger(__FILE__, __LINE__).stream()
#define LOG_WARN if (Logger::level() <= Logger::WARN) \
    Logger(__FILE__, __LINE__, Logger::WARN).stream()
#define LOG_ERROR if (Logger::level() <= Logger::ERROR) \
    Logger(__FILE__, __LINE__, Logger::ERROR).stream()
#define LOG_FATAL if (Logger::level() <= Logger::FATAL) \
    Logger(__FILE__, __LINE__, Logger::FATAL).stream()

#endif