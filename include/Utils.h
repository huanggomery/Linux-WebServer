// 一些小工具
#ifndef _UTILS_H
#define _UTILS_H
#include <string>
#include <sys/socket.h>
#include <arpa/inet.h>

#define TESTOUT std::cout << "this is just a test" << std::endl;

// 创建服务器套接字,如果失败，返回-1
int Create_And_Listen(int port);

// 设置为非阻塞模式
bool SetSocketNoBlocking(int fd);

// 信号处理函数，使用pipe告知epoll
void sig_hander(int sig);

// 设置信号的信号处理函数，默认为sig_handler
bool addsig(int sig, void (*handler)(int) = sig_hander);

// 返回当前时间的字符串,格式类似于： 20230706 21:05:57.229383
std::string get_time_str();

// 返回当前GMT时间字符串，格式类似于： Tue, 11 Jul 2023 07:22:04 GMT
std::string get_gmt_time_str();

// 生成日志文件名
std::string get_logfile_name();

// 根据sockaddr_in，返回IP地址的点分十进制字符串
std::string dotted_decimal_notation(const sockaddr_in &addr);

// 根据sockaddr_in，返回源端口号
int src_port(const sockaddr_in &addr);

#endif