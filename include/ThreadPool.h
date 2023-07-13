// 定义线程池类模板
#ifndef _THREADPOOL_H
#define _THREADPOOL_H
#include <pthread.h>
#include <vector>
#include <list>
#include <memory>
#include <iostream>
#include "Sync.h"
#include "Logging.h"
using std::vector;
using std::list;
using std::shared_ptr;
using std::weak_ptr;


enum ThreadPoolError {
    EXISTED = -1,
    THREAD_CREATING_ERROR
};

template <typename T>
class ThreadPool
{
    using SP_Task = shared_ptr<T>;
private:
    bool stop_;
    int threadNum_;    // 线程数
    int started_;      // 当前正在运行的线程数
    int maxQueue_;      // 工作队列中最大等待个数
    vector<pthread_t> threads_;  // 线程
    list<SP_Task> workqueue_;    // 工作队列
    Locker locker_;
    Conditon cond_;
    static void *run(void *arg);   // 工作线程运行函数
    ThreadPool(int n, int maxq);    // 构造函数，私有
    void wait_threads();   // 等待所有子线程退出
    static weak_ptr<ThreadPool<T>> pool_;   // 静态指针，指向单例模式的唯一线程池

public:
    static shared_ptr<ThreadPool<T>> CreateThreadPool(int n, int maxq);   // 工厂函数
    bool addTask(SP_Task task);
    void shutdown();    // 结束，退出所有线程
    ThreadPool() = delete;
    ThreadPool(const ThreadPool &) = delete;
    ThreadPool &operator=(const ThreadPool &) = delete;
    bool isStop() const;
    
};
template <typename T>
weak_ptr<ThreadPool<T>> ThreadPool<T>::pool_;


template <typename T>
ThreadPool<T>::ThreadPool(int n, int maxq):
    stop_(false), threadNum_(n), started_(0), threads_(n,0),
    maxQueue_(maxq), locker_(), cond_(locker_){}


// 工厂函数，创建线程池
template <typename T>
shared_ptr<ThreadPool<T>> ThreadPool<T>::CreateThreadPool(int n, int maxq)  // 线程数量和工作队列最大长度
{
    if (pool_.lock())
        return nullptr;
    
    shared_ptr<ThreadPool> sp(nullptr);
    try
    {
        sp.reset(new ThreadPool(n, maxq));
        pool_ = sp;
    }
    catch (const std::bad_alloc &e)
    {
        std::cerr << "malloc error: " << e.what() << std::endl;
        return nullptr;
    }

    for (int i = 0; i < n; ++i)
    {
        if (pthread_create(&(sp->threads_[i]), NULL, run, NULL) != 0)
        {
            sp->shutdown();
            return nullptr;
        }
        ++sp->started_;
    }
    return pool_.lock();
}


// 向工作队列添加任务
template <typename T>
bool ThreadPool<T>::addTask(SP_Task task)
{
    if (workqueue_.size() >= maxQueue_)
        return false;
    
    locker_.lock();
    if (stop_)
    {
        locker_.unlock();
        return false;
    }
    workqueue_.push_back(task);
    locker_.unlock();
    cond_.signal();
    return true;
}


// 工作线程运行函数
template <typename T>
void *ThreadPool<T>::run(void *arg)
{
    auto sp = pool_.lock();
    if (!sp)
        return NULL;
    while (!sp->stop_)
    {
        sp->locker_.lock();
        while (!sp->stop_ && sp->workqueue_.empty())
            sp->cond_.wait();

        if (sp->stop_)
            break;
        
        SP_Task task = sp->workqueue_.front();
        sp->workqueue_.pop_front();
        sp->locker_.unlock();
        task->process();
    }

    --sp->started_;
    sp->locker_.unlock();
    return NULL;
}

template <typename T>
void ThreadPool<T>::shutdown()
{
    locker_.lock();
    stop_ = true;
    locker_.unlock();
    cond_.broadcast();
    for (auto tid : threads_)
    {
        if (tid == 0)
            break;
        // std::cout << "wait for " << tid << std::endl; 
        pthread_join(tid, NULL);
    }
}

template <typename T>
bool ThreadPool<T>::isStop() const
{
    return stop_;
}

#endif