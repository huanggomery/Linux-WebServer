// 用于重载<<操作符，并定义了日志缓冲区
#ifndef _LOGSTREAM_H
#define _LOGSTREAM_H
#include "noncopyable.h"
#include "stdio.h"
#include <cstring>
#include <string>

const int SmallBufferSize = 4000;        // 单条日志记录的缓冲区大小
const int LargeBufferSize = 4000*1000;   // 每个前端缓冲区大小 
const int MaxNumericSize = 48;           // 数值型的最长长度

template <int SIZE>
class FixedBuffer: public noncopyable
{
public:
    FixedBuffer(): cur(data){};
    ~FixedBuffer() = default;
    size_t avail() {return data + sizeof(data) - cur;}
    void reset() {cur = data;}
    void bzero() {memset(data, 0, SIZE);}
    void append(const char *logline, int len)
    {
        if (avail() > len)
        {
            memcpy(cur, logline, len);
            cur += len;
        }
    }
    const char *getData() const {return data;}
    size_t size() const {return cur-data;}
    const char *current() const {return cur;}

private:
    char data[SIZE];
    char *cur;
};

class LogStream: public noncopyable
{
    using self = LogStream;
    using Buffer = FixedBuffer<SmallBufferSize>;
public:
    const Buffer &buffer() const {return buffer_;}
    self &operator<<(bool v)
    {
        buffer_.append(v ? "1" : "0", 1);
        return *this;
    }

    self& operator<<(short);
    self& operator<<(unsigned short);
    self& operator<<(int);
    self& operator<<(unsigned int);
    self& operator<<(long);
    self& operator<<(unsigned long);
    self& operator<<(long long);
    self& operator<<(unsigned long long);

    // self& operator<<(void *);

    self& operator<<(float v);
    self& operator<<(double v);

    self& operator<<(char v)
    {
        buffer_.append(&v, 1);
        return *this;
    }

    self& operator<<(const char *str)
    {
        if (str)
            buffer_.append(str, strlen(str));
        else
            buffer_.append("(null)", 6);
        return *this;
    }

    self& operator<<(const std::string &v)
    {
        buffer_.append(v.c_str(), v.size());
        return *this;
    }

private:
    void append(const char *data, int len) {buffer_.append(data, len);}
    Buffer buffer_;
};

#endif