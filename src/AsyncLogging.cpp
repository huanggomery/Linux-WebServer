#include "AsyncLogging.h"
#include "Utils.h"
#include <algorithm>

namespace {
    void *threadFunction(void *arg)
    {
        AsyncLogging *AsyncLogger_ = reinterpret_cast<AsyncLogging*>(arg);
        AsyncLogger_->threadFunc();
        return NULL;
    }
} // namespace

std::vector<std::string> AsyncLogging::fileOpened_;

AsyncLogging::AsyncLogging(const std::string &filename,int flushIntervel):
    file_(fopen(filename.c_str(), "a")),
    filename_(filename),
    flushInterval_(flushIntervel),
    running_(false),
    locker_(),
    cond_(locker_),
    currentBuffer_(new Buffer),
    nextBuffer_(new Buffer)
{
    if (std::find(fileOpened_.begin(), fileOpened_.end(), filename) != fileOpened_.end())
        throw std::runtime_error("Filename has been opened");

    fileOpened_.push_back(filename);
    currentBuffer_->bzero();
    nextBuffer_->bzero();
    buffer_.reserve(16);
}

AsyncLogging::~AsyncLogging()
{
    if (running_)
        stop();
}

void AsyncLogging::start() 
{
    running_ = true;
    pthread_create(&tid_, NULL, threadFunction, this);
}

void AsyncLogging::stop()
{
    running_ = false;
    cond_.signal();
    pthread_join(tid_, NULL);
}

void AsyncLogging::append(const char *logline, int len)
{
    locker_.lock();
    if (currentBuffer_->avail() >= len)
    {
        currentBuffer_->append(logline, len);  // 最多的情况
    }
    else
    {
        buffer_.push_back(std::move(currentBuffer_));
        if (nextBuffer_)
            currentBuffer_.reset(nextBuffer_.release());   // 较少发生
        else
            currentBuffer_.reset(new Buffer);    // 很少发生
        currentBuffer_->append(logline, len);
        cond_.signal();
    }

    locker_.unlock();
}

void AsyncLogging::threadFunc()
{
    BufferPtr newBuffer1(new Buffer);
    BufferPtr newBuffer2(new Buffer);
    newBuffer1->bzero();
    newBuffer2->bzero();
    BufferVector buffersToWrite;
    buffersToWrite.reserve(16);

    do
    {
        locker_.lock();

        if (buffer_.empty())
            cond_.waitForSeconds(flushInterval_);
        buffer_.push_back(std::move(currentBuffer_));
        currentBuffer_.reset(newBuffer1.release());
        buffersToWrite.swap(buffer_);
        if (!nextBuffer_)
            nextBuffer_.reset(newBuffer2.release());

        locker_.unlock();

        char errorBuf[256] = {0};
        // 丢弃过多的数据
        if (buffersToWrite.size() > 25)
        {
            snprintf(errorBuf, sizeof(errorBuf), "Dropped log messages at %s, %zd larger buffers\n",
                     get_time_str().c_str(), buffersToWrite.size()-2);
            
            buffersToWrite.erase(buffersToWrite.begin() +2, buffersToWrite.end());
        }

        // 写入硬盘
        for (auto &bp : buffersToWrite)
        {
            fwrite(bp->getData(), 1, bp->size(), file_);
        }
        if (strlen(errorBuf) > 0)
            fwrite(errorBuf, 1, strlen(errorBuf), file_);
        
        // 重新填充newBuffer1和newBuffer2
        if (!newBuffer1)
        {
            newBuffer1.reset(buffersToWrite.back().release());
            buffersToWrite.pop_back();
            newBuffer1->reset();
        }
        if (!newBuffer2)
        {
            newBuffer2.reset(buffersToWrite.back().release());
            buffersToWrite.pop_back();
            newBuffer2->reset();
        }
        buffersToWrite.clear();

        // flush
        fflush(file_);
    } while (running_);

    fflush(file_);
    fclose(file_);
    auto it = std::find(fileOpened_.begin(), fileOpened_.end(), filename_);
    if (it != fileOpened_.end())
        fileOpened_.erase(it);
}