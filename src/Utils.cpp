#include "Utils.h"
#include "Logging.h"
#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/epoll.h>


// 创建服务器套接字,如果失败，返回-1
int Create_And_Listen(int port)
{
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);

    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    if (listenfd == -1)
        return -1;
    
    // 使用的时候注释掉
    int reuse = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    if (bind(listenfd, (sockaddr *)&address, sizeof(address)) == -1)
    {
        LOG_DEBUG << "Tring to bind port " << port << " failed";
        close(listenfd);
        return -1;
    }
    if (listen(listenfd, 2048) == -1)
    {
        close(listenfd);
        return -1;
    }
    if (!SetSocketNoBlocking(listenfd))
    {
        close(listenfd);
        return -1;
    }
    return listenfd;
}

// 设置为非阻塞模式
bool SetSocketNoBlocking(int fd)
{
    int old_opt = fcntl(fd, F_GETFL);
    if (fcntl(fd, F_SETFL, old_opt | O_NONBLOCK) == -1)
        return false;
    return true;
}



int pipefd[2];   // 用于处理信号的管道
// 信号处理函数，使用pipe告知epoll
void sig_hander(int sig)
{
    // 保存errno，在函数最后恢复，以保证函数的可重入性
    int old_errno = errno;
    send(pipefd[1], &sig, 1, 0);
    errno = old_errno;
}

// 设置信号的信号处理函数，默认为sig_handler
bool addsig(int sig, void (*handler)(int))
{
    struct sigaction act;
    memset(&act, 0, sizeof(act));
    act.sa_flags |= SA_RESTART;
    sigfillset(&act.sa_mask);
    act.sa_handler = handler;
    if (sigaction(sig, &act, NULL) == -1)
        return false;
    return true;
}

// 返回当前时间的字符串,格式类似于： 20230706 21:05:57.229383
std::string get_time_str()
{
    char str_t[25] = {0};
    struct timeval tv;
    gettimeofday(&tv, NULL);
    time_t sec = tv.tv_sec;
    struct tm *ptime = localtime(&sec);
    strftime(str_t, 26, "%Y%m%d %H:%M:%S", ptime);

    char usec[8];
    snprintf(usec, sizeof(usec), ".%ld", tv.tv_usec);
    strcat(str_t, usec);

    return std::string(str_t);
}

// 返回当前GMT时间字符串，格式类似于： Tue, 11 Jul 2023 07:22:04 GMT
std::string get_gmt_time_str()
{
    time_t currentTime = time(NULL);
    struct tm *gmTime = gmtime(&currentTime);
    char timeString[50];

    strftime(timeString, sizeof(timeString), "%a, %d %b %Y %H:%M:%S GMT", gmTime);
    return std::string(timeString);
}

// 生成日志文件名
std::string get_logfile_name()
{
    char ret[27] = "WebServer";
    char str_t[25] = {0};
    struct timeval tv;
    gettimeofday(&tv, NULL);
    time_t sec = tv.tv_sec;
    struct tm *ptime = localtime(&sec);
    strftime(str_t, 26, "%Y%m%d%H%M%S", ptime);

    strcat(ret, str_t);
    strcat(ret, ".log");

    return std::string(ret);
}

// 根据sockaddr_in，返回IP地址的点分十进制字符串
std::string dotted_decimal_notation(const sockaddr_in &addr)
{
    return std::string(inet_ntoa(addr.sin_addr));
}