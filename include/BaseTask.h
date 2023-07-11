// 所有任务类都应该继承这个基类
#ifndef _BASETASK_H
#define _BASETASK_H
#include <unistd.h>
#include <memory>
#include "Epoll.h"
#include "Timer.h"
#include <sys/socket.h>
#include <arpa/inet.h>
#include "noncopyable.h"
using std::shared_ptr;
// #define TaskType std::remove_reference<decltype(*this)>::type;

/*
    任务类编写说明：
    首先一定要继承基类BaseTask

    一个任务类对象的智能指针存在于定时器和Epoll的fd2task中。
    Epoll处理时不会改动定时器和fd2task中的任务指针
        任务类需要自己管理定时器和epoll监测事件
        所以如果任务处理时希望更新该任务的定时器，需要与定时器双向解耦，然后重新添加
        如果希望与客户端断开连接，需要和定时器双向解耦并手动调用epoll_del，对象才能被析构

    任务类中应该包含的变量和函数参照BaseTask类中的说明
*/


class BaseTask
{
    /*
        应该包含如下类型名声明：
        using SP_TimerManager = shared_ptr<TimerManager<TaskType>>;
        using SP_Timer = shared_ptr<TimerNode<TaskType>>;
        using SP_Epoll = shared_ptr<Epoll<TaskType>>;
    */
public:
    BaseTask() = delete;
    BaseTask(int sock, sockaddr_in addr): sock_(sock), addr_(addr){}
    ~BaseTask() {close(sock_);}
    virtual void process() = 0;
    int getsock() const {return sock_;}
    sockaddr_in getaddr() const {return addr_;}
    /*
        在其派生类中应该定义以下成员函数：
        TaskType(int sock, sockaddr_in addr);   构造函数
        void linkTimer(SP_Timer timer);
        void separateTimer();   // 与定时器单向解耦，即定时器删除时会连带删除任务，任务类中通常不调用这个函数
        void bilateralSeparateTimer(); // 与定时器双向解耦，即定时器删除时不会删除任务
        static void Init(SP_TimerManager, SP_Epoll);
        void process() override;  业务函数，必须重新的纯虚函数
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