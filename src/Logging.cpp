#include "Logging.h"
#include "Utils.h"
#include <pthread.h>
#include <unistd.h>
#include <string>
#include <memory>

// 全局的日志级别
Logger::LogLevel Logger::g_level_(INFO);

std::string LevelStr[] = {
    "TRACE",
    "DEBUG",
    "INFO",
    "WARN",
    "ERROR",
    "FATAL"
};

namespace {
    pthread_once_t once = PTHREAD_ONCE_INIT;
    std::shared_ptr<AsyncLogging> AsyncLogger_(nullptr);
} // namespace


void init_function()
{
    AsyncLogger_.reset(new AsyncLogging(get_logfile_name()));
    AsyncLogger_->start();
}

// 往异步日志的前端写
void output(const char *logline, int len)
{
    pthread_once(&once, init_function);
    AsyncLogger_->append(logline, len);
}

Logger::Logger(const std::string &basename, int line, LogLevel level):
    impl_(basename, line, level)
{

}

Logger::~Logger()
{

}

Logger::Impl::Impl(const std::string &basename, int line, LogLevel level):
    stream_(), basename_(basename), line_(line), level_(level)
{
    stream_ << get_time_str() <<  " " << gettid() << " " << LevelStr[level] << " ";
}

Logger::Impl::~Impl()
{
    stream_ << " - " << basename_ << ":" << line_ << '\n';
    output(stream_.buffer().getData(), stream_.buffer().size());
}