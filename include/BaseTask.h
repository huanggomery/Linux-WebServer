// 所有任务类都应该继承这个基类
#ifndef _BASETASK_H
#define _BASETASK_H
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "noncopyable.h"
// #define TaskType std::remove_reference<decltype(*this)>::type;


class BaseTask
{
public:
    BaseTask() = delete;
    BaseTask(int sock, sockaddr_in addr): sock_(sock), addr_(addr){}
    ~BaseTask() {close(sock_);}
    virtual void process() = 0;
    int getsock() const {return sock_;}
    sockaddr_in getaddr() const {return addr_;}
    /*
        在其派生类中应该定义以下成员函数：
        TaskType(int sock);   构造函数
        void linkTimer(SP_Timer timer);
        void separateTimer();   // 与定时器单向解耦，即定时器删除时会连带删除任务
        void bilateralSeparateTimer(); // 与定时器双向解耦，即定时器删除时不会删除任务
        static void Init(SP_TimerManager, SP_Epoll);
    */

protected:
    int sock_;
    sockaddr_in addr_;
    /*
        在其派生类中应该定义以下成员：
        SP_Timer timer_;
        static SP_TimerManager timer_manager_;  // 业务处理时需要添加计时器
        static SP_Epoll epoll_;   // 业务处理时需要修改监听的类别
    */
};

#endif