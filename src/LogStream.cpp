#include "LogStream.h"
#include <algorithm>

const char digits[] = "9876543210123456789";
const char *zero = digits + 9;

template <typename Integer>
void formatInteger(FixedBuffer<SmallBufferSize> &buf, Integer value)
{
    if (buf.avail() < MaxNumericSize)
        return;
    char ibuf[MaxNumericSize];
    char *tmp = ibuf;
    Integer i = value;
    do
    {
        int lsd = i % 10;
        *tmp++ = zero[lsd];
        i /= 10;
    } while (i != 0);
    
    if (value < 0)
        *tmp++ = '-';
    *tmp = '\0';
    std::reverse(ibuf,tmp);
    buf.append(ibuf, tmp-ibuf);
}

LogStream &LogStream::operator<<(short v)
{
    (*this) << static_cast<int>(v);
    return *this;
}

LogStream &LogStream::operator<<(unsigned short v)
{
    (*this) << static_cast<unsigned int>(v);
    return *this;
}

LogStream &LogStream::operator<<(int v)
{
    formatInteger(buffer_, v);
    return *this;
}

LogStream &LogStream::operator<<(unsigned int v)
{
    formatInteger(buffer_, v);
    return *this;
}

LogStream &LogStream::operator<<(long v)
{
    formatInteger(buffer_, v);
    return *this;
}

LogStream &LogStream::operator<<(unsigned long v)
{
    formatInteger(buffer_, v);
    return *this;
}

LogStream &LogStream::operator<<(long long v)
{
    formatInteger(buffer_, v);
    return *this;
}

LogStream &LogStream::operator<<(unsigned long long v)
{
    formatInteger(buffer_, v);
    return *this;
}

LogStream &LogStream::operator<<(float v)
{
    *this << static_cast<double>(v);
    return *this;
}

LogStream &LogStream::operator<<(double v)
{
    if (buffer_.avail() > MaxNumericSize)
    {
        char buf[MaxNumericSize] = {0};
        snprintf(buf, MaxNumericSize, "%.12g", v);
        buffer_.append(buf, strlen(buf));
    }
    return *this;
}