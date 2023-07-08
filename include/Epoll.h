// 定义Epoll类
#ifndef _EPOLL_H
#define _EPOLL_H
#include <sys/epoll.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <unistd.h>
#include <memory>
#include <iostream>
#include <cstring>
#include <vector>
#include <exception>
#include <signal.h>
#include "ThreadPool.h"
#include "Timer.h"
#include "Utils.h"
#include "Logging.h"
using std::shared_ptr;
using std::weak_ptr;
using std::vector;

const int MAXFD = 1024;
extern int pipefd[2];   // 用于传递信号的管道，在Utils.cpp中定义

template <typename T>
class Epoll
{
    using SP_Task = shared_ptr<T>;
    using SP_Self = shared_ptr<Epoll<T>>;
    using WP_Self = weak_ptr<Epoll<T>>;
    using SP_TimerManager = shared_ptr<TimerManager<T>>;
    using SP_ThreadPool = shared_ptr<ThreadPool<T>>;
public:
    static SP_Self CreateEpoll(SP_ThreadPool tp, int port, int timeout);
    void epoll_wait_and_handle();
    bool epoll_add(int fd, int ev, SP_Task task);
    bool epoll_mod(int fd, int ev, SP_Task task);
    bool epoll_del(int fd);
    Epoll() = delete;
    Epoll(const Epoll &) = delete;
    Epoll &operator=(const Epoll &) = delete;
    ~Epoll() = default;
    
private:
    static WP_Self epoll_;     // 单例模式指向唯一实体
    SP_ThreadPool pool_;   // 线程池指针
    SP_TimerManager timer_manager_;   // 定时器管理者
    int epfd_;
    int listenfd_;
    int timeout_;     // 新连接来时的初始计时器
    epoll_event events_[MAXFD];   // 用来保存epoll_wait得到的事件
    SP_Task fd2Task[MAXFD];      // 保持文件描述符到Task的映射
    Epoll(shared_ptr<ThreadPool<T>> tp,int port, int timeout);
    vector<SP_Task> getEventsRequest(int num);   // 在epoll_wait后调用这个函数，返回任务的vector
    void acceptConnection();        // 接受新的连接
    void handleSignal();            // 处理信号
};
template <typename T>
weak_ptr<Epoll<T>> Epoll<T>::epoll_;

// 构造函数，需要创建epollfd
template <typename T>
Epoll<T>::Epoll(shared_ptr<ThreadPool<T>> tp,int port, int timeout):
    epfd_(epoll_create(MAXFD)), listenfd_(Create_And_Listen(port)), 
    pool_(tp),timeout_(timeout), fd2Task{nullptr}, timer_manager_(nullptr)
{
    if (epfd_ < 0)
        throw std::runtime_error("Epoll create failed");
    if (listenfd_ < 0)
        throw std::runtime_error("Socket create failed");
}

// 工厂函数，需要传入线程池
template <typename T>
shared_ptr<Epoll<T>> Epoll<T>::CreateEpoll(shared_ptr<ThreadPool<T>> tp, int port, int timeout)
{
    if (epoll_.lock())
        return nullptr;
    
    SP_Self sp(nullptr);
    try
    {
        sp.reset(new Epoll<T>(tp, port, timeout));
        epoll_ = sp;
    }
    catch (const std::bad_alloc &e)
    {
        std::cerr << "malloc error: " << e.what() << std::endl;
        return nullptr;
    }
    catch (const std::runtime_error &e)
    {
        std::cerr << "runtime error: " << e.what() << std::endl;
        return nullptr;
    }

    // 把listenfd注册到epoll
    if (!sp->epoll_add(sp->listenfd_, EPOLLIN | EPOLLET, nullptr))
    {
        std::cerr << "epoll_add failed" << std::endl;
        return nullptr;
    }

    // 信号处理
    if (socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd) == -1)
    {
        std::cerr << "socketpair failed" << std::endl;
        return nullptr;
    }
    SetSocketNoBlocking(pipefd[1]);
    sp->epoll_add(pipefd[0], EPOLLIN | EPOLLET, nullptr);
    
    bool ret = true;
    ret = ret && addsig(SIGINT);
    ret = ret && addsig(SIGTERM);
    if (!ret)
    {
        std::cerr << "add signal failed" << std::endl;
        return nullptr;
    }

    sp->timer_manager_ = TimerManager<T>::CreateTimerManager(sp);
    T::Init(sp->timer_manager_, sp);
    return epoll_.lock();
}

template <typename T>
bool Epoll<T>::epoll_add(int fd, int ev, SP_Task task)
{
    epoll_event event;
    memset(&event, 0, sizeof(event));
    event.data.fd = fd;
    event.events = ev;
    if (epoll_ctl(epfd_, EPOLL_CTL_ADD, fd, &event) != 0)
        return false;
    fd2Task[fd] = task;
    return true;
}

template <typename T>
bool Epoll<T>::epoll_mod(int fd, int ev, SP_Task task)
{
    epoll_event event;
    memset(&event, 0, sizeof(event));
    event.data.fd = fd;
    event.events = ev;
    if( epoll_ctl(epfd_, EPOLL_CTL_MOD, fd, &event) != 0)
    {
        fd2Task[fd].reset();
        return false;
    }
    fd2Task[fd] = task;
    return true;
}

template <typename T>
bool Epoll<T>::epoll_del(int fd)
{
    if (epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, nullptr) != 0)
        return false;
    fd2Task[fd].reset();
    return true;
}

// 获取IO事件，并将Task加入工作队列
template <typename T>
void Epoll<T>::epoll_wait_and_handle()
{
    int num = epoll_wait(epfd_, events_, MAXFD, 5000);
    if (num == -1 && errno != EINTR)
    {
        // std::cerr << "epoll_wait failed" << std::endl;
        LOG_ERROR << "epoll_wait failed";
        return;
    }
    // std::cout << "num = " << num << std::endl;
    vector<SP_Task> requests = getEventsRequest(num);
    for (auto &p : requests)
    {
        if (!pool_->addTask(p))
            break;      // 由于线程池的工作队列已满或者线程池已关闭，放弃本次监听的事件
    }
    timer_manager_->handleExpired();  // 处理超时的定时器
}

// 从events中获取事件，并把事件对应的Task指针存到vector中返回
template <typename T>
vector<shared_ptr<T>> Epoll<T>::getEventsRequest(int num)
{
    vector<SP_Task> requests;
    for (int i = 0; i < num; ++i)
    {
        int fd = events_[i].data.fd;
        int ev = events_[i].events;
        // 新的用户连接
        if (fd == listenfd_)
            acceptConnection();
        else if ((fd == pipefd[0]) &&  (ev & EPOLLIN))
            handleSignal();
        else if ((ev & EPOLLIN) || (ev & EPOLLOUT))
            requests.push_back(fd2Task[fd]);
        else {/* something else */}
    }
    return requests;
}

// 接受新的连接
template <typename T>
void Epoll<T>::acceptConnection()
{
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    socklen_t addr_len = sizeof(addr);
    int connfd;
    while ((connfd = accept(listenfd_, (sockaddr *)&addr, &addr_len)) != -1)
    {
        LOG_INFO << "accept new connection, socket: " << connfd << " ip: " << inet_ntoa(addr.sin_addr);
        if (connfd >= MAXFD)
        {
            close(connfd);
            LOG_ERROR << "connfd >= MAXFD, close the socket";
            continue;
        }
        if (!SetSocketNoBlocking(connfd))
        {
            close(connfd);
            // std::cerr << "Set no blocking failed" << std::endl;
            LOG_ERROR << "Set no blocking failed, close the socket";
            return;
        }

        SP_Task new_task(new T(connfd, addr));
        if (!epoll_add(connfd, EPOLLIN | EPOLLET | EPOLLONESHOT, new_task))
        {
            close(connfd);
            // std::cerr << "epoll_add failed" << std::endl;
            LOG_ERROR <<"epoll_add failed";
            return;
        }
        if (!timer_manager_->addTimer(new_task, timeout_))
        {
            close(connfd);
            epoll_del(connfd);
            // std::cerr << "Add timer failed" << std::endl;
            LOG_ERROR << "Add timer failed";
            return;
        }
    }
}

template <typename T>
void Epoll<T>::handleSignal()
{
    char signals[1024];
    int ret = recv(pipefd[0], signals, 1024, 0);
    if (ret <= 0)
        return;
    for (int i = 0; i < ret; ++i)
    {
        switch (signals[i])
        {
            case SIGINT:
            case SIGTERM:
                LOG_WARN << "get SIGINT or SIGTERM";
                pool_->shutdown();
                break;
            default:
                // std::cout << "get some signals, and don't know how to handle." << std::endl;
                LOG_WARN << "get some signals, and don't know how to handle.";
        }
    }
}

#endif