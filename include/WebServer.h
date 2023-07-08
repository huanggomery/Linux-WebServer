// WebServer模板类，把所有东西都整合起来
#include "ThreadPool.h"
#include "Epoll.h"
#include "Logging.h"
#include <memory>
#include <exception>
using std::shared_ptr;
using std::weak_ptr;

// const int THREAD_NUM = 16;
// const int MAX_QUEUE = 10000;


// 也是单例模式
template <typename T>
class WebServer
{
    using SP_Self = shared_ptr<WebServer<T>>;
    using WP_Self = weak_ptr<WebServer<T>>;
    using SP_ThreadPool = shared_ptr<ThreadPool<T>>;
    using SP_Epoll = shared_ptr<Epoll<T>>;
private:
    static WP_Self self_;
    SP_ThreadPool pool_;
    SP_Epoll epoll_;
    WebServer(int thread_num, int maxq, int port, int timeout);
    
public:
    static SP_Self CreateWebServer(int port, int timeout, int thread_num, int maxq);
    void work();
};

template <typename T>
weak_ptr<WebServer<T>> WebServer<T>::self_;


template <typename T>
WebServer<T>::WebServer(int port, int timeout, int thread_num, int maxq)
{
    pool_ = ThreadPool<T>::CreateThreadPool(thread_num, maxq);
    if (!pool_)
        throw std::runtime_error("Thread Pool failed");
    epoll_ = Epoll<T>::CreateEpoll(pool_, port, timeout);
    if (!epoll_)
        throw std::runtime_error("Epoll failed");
}

template <typename T>
shared_ptr<WebServer<T>> WebServer<T>::CreateWebServer(int port, int timeout, int thread_num, int maxq)
{
    if (self_.lock())
        return nullptr;
    
    SP_Self sp(nullptr);
    try
    {
        sp.reset(new WebServer<T>(port, timeout, thread_num, maxq));
        self_ = sp;
    }
    catch (const std::bad_alloc &e)
    {
        // std::cerr << "malloc error: " << e.what() << std::endl;
        LOG_FATAL << "malloc error: " << e.what();
        return nullptr;
    }
    catch (const std::runtime_error &e)
    {
        // std::cerr << "runtime error: " << e.what() << std::endl;
        LOG_FATAL << "runtime error: " << e.what();
        return nullptr;
    }
    LOG_INFO << "Server started";
    return self_.lock();
}


template <typename T>
void WebServer<T>::work()
{
    while (!pool_->isStop())
    {
        epoll_->epoll_wait_and_handle();
    }
    LOG_INFO << "Server stopped";
}