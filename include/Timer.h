// 定义定时器
#ifndef _TIMER_H
#define _TIMER_H
#include <sys/time.h>
#include <memory>
#include <queue>
#include <vector>
#include "Sync.h"
#include "Logging.h"
using std::shared_ptr;
using std::weak_ptr;

template <typename T> class Epoll;  // 前向声明

template <typename T>
class TimerNode
{
    using SP_Task = shared_ptr<T>;
    using SP_Epoll = shared_ptr<Epoll<T>>;
public:
    TimerNode() = delete;
    TimerNode(const TimerNode &) = delete;               // 禁止拷贝构造
    TimerNode &operator=(const TimerNode &) = delete;    // 禁止拷贝赋值
    TimerNode(SP_Task task, int timeout);   // 构造函数，需要传入任务指针和计时时间（毫秒）
    ~TimerNode();
    time_t getExpTime() const;     // 返回超时时间
    void setDeleted();             // 将定时器设为删除的
    void separate();               // 将定时器和任务分离
    bool isVaild();                // 验证定时器是否有效，如果已经超时，则设为删除的
    static void setEpoll(SP_Epoll epoll);    // 静态函数，设置epoll_

private:
    bool deleted;
    time_t expired_time_;
    SP_Task task_;
    static SP_Epoll epoll_;
};

template <typename T> 
shared_ptr<Epoll<T>> TimerNode<T>::epoll_(nullptr);

// 仿函数，用于在优先队列中对定时器进行排序
template <typename T>
struct CmpTimer
{
    using SP_Timer = shared_ptr<TimerNode<T>>;
    bool operator()(const SP_Timer &t1, const SP_Timer &t2)
    {
        return t1->getExpTime() > t2->getExpTime();
    }
};


// 管理定时器队列，单例模式
template <typename T>
class TimerManager
{
    using SP_Task = shared_ptr<T>;
    using SP_Timer = shared_ptr<TimerNode<T>>;
    using SP_Self = shared_ptr<TimerManager<T>>;
    using WP_Self = weak_ptr<TimerManager<T>>;
public:
    static SP_Self CreateTimerManager(shared_ptr<Epoll<T>> epoll);  // 工厂函数
    bool addTimer(SP_Task task, int timeout);
    void handleExpired();

private:
    TimerManager() = default;
    TimerManager(const TimerManager &) = delete;
    TimerManager &operator=(const TimerManager &) = delete;

    Locker locker_;
    std::priority_queue<SP_Timer, std::vector<SP_Timer>, CmpTimer<T>> timer_queue_;
    static WP_Self manager_;
};

template <typename T>
weak_ptr<TimerManager<T>> TimerManager<T>::manager_;

/* ****************成员函数定义部分********************* */
template <typename T>
TimerNode<T>::TimerNode(SP_Task task, int timeout): task_(task), deleted(false)
{
    timeval now;
    gettimeofday(&now, NULL);
    expired_time_ = now.tv_sec * 1000 + now.tv_usec / 1000 + timeout;
}

template <typename T>
TimerNode<T>::~TimerNode()
{
    if (task_)
    {
        LOG_INFO << "timeout, socket: " << task_->getsock() << " ip: " << inet_ntoa(task_->getaddr().sin_addr);
        epoll_->epoll_del(task_->getsock());
    }
}

template <typename T>
time_t TimerNode<T>::getExpTime() const
{
    return expired_time_;
}

template <typename T>
void TimerNode<T>::setDeleted()
{
    deleted = true;
}

template <typename T>
void TimerNode<T>::separate()
{
    if (task_)
        task_.reset();
    setDeleted();
}

template <typename T>
bool TimerNode<T>::isVaild()
{
    if (deleted)
        return false;
    timeval now;
    gettimeofday(&now, NULL);
    time_t nowtime = now.tv_sec * 1000 + now.tv_usec / 1000;
    if (nowtime < expired_time_)
        return true;
    // 如果计时器到期了，需要将其与任务解耦
    else
    {
        setDeleted();
        if (task_)
            task_->separateTimer();
        return false;
    }
}

template <typename T>
void TimerNode<T>::setEpoll(SP_Epoll epoll)
{
    epoll_ = epoll;
}


/* -------------------分割线-----------------------*/


template <typename T>
shared_ptr<TimerManager<T>> TimerManager<T>::CreateTimerManager(shared_ptr<Epoll<T>> epoll)
{
    if (manager_.lock())
        return nullptr;
    
    SP_Self sp(nullptr);
    try
    {
        sp.reset(new TimerManager());
        manager_ = sp;
    }
    catch(const std::bad_alloc &e)
    {
        std::cerr << "malloc error: " << e.what() << std::endl;
        return nullptr;
    }

    TimerNode<T>::setEpoll(epoll);   // 设置TimerNode中的静态成员
    return manager_.lock();
}

template <typename T>
bool TimerManager<T>::addTimer(SP_Task task, int timeout)
{
    // 如果timeout < 0，就是不设置计时器
    if (timeout < 0)
        return true;

    // 先创建一个新的TimerNode节点
    SP_Timer new_timer(nullptr);
    try
    {
        new_timer.reset(new TimerNode<T>(task, timeout));
    }
    catch(const std::bad_alloc &e)
    {
        std::cerr << "TimerNode Creating failed: " << e.what() << '\n';
        return false;
    }

    // 然后把定时器指针添加到优先队列中
    locker_.lock();
    timer_queue_.push(new_timer);
    locker_.unlock();

    // 将计时器与任务关联
    task->linkTimer(new_timer);

    return true;
}


/*
任务和计时器对象的生命周期：
    在以下两个地方存在任务对象的智能指针：
        1. Epoll类中有fd2task[]
        2. TimerNode计时器对象中
    
    在以下两个地方存在计时器对象的智能指针：
        1. 计时器队列timer_queue_
        2. 任务对象中 （task->linkTimer(new_timer);）

    销毁一个计时器对象的两种方式：
        1. 计时器超时
            执行isVaild()函数时，如果发现超时，则从任务对象中删除该计时器的指针（单向分离），然后从timer_queue_中pop，
            于是计时器智能指针的引用为0,执行计时器对象的析构函数。在析构函数中，删除fd2task[]中的任务对象指针，
            然后自身析构。此时任务对象的智能指针的引用为0,删除任务对象
        2. 任务对象收到新消息
            任务对象和计时器双向分离，计时器设置deleted，以后的某个时间从timer_queue_中pop，于是计时器智能指针的引用为0,
            执行计时器对象的析构函数。在析构函数中，由于已经双向分离，所以无法从fd2task[]中的任务对象指针，所以只析构计时器对象，
            任务对象依旧存在。
*/
template <typename T>
void TimerManager<T>::handleExpired()
{
    locker_.lock();
    while (!timer_queue_.empty())
    {
        auto &node = timer_queue_.top();
        if (node->isVaild())
            break;
        else
        {
            timer_queue_.pop();
            // std::cout << "删除定时器" << std::endl;
        }
    }
    locker_.unlock();
}

#endif