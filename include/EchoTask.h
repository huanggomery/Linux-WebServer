#ifndef _TASK_H
#define _TASK_H
#include "BaseTask.h"
#include "Epoll.h"
#include "Timer.h"
#include <memory>
using std::shared_ptr;

const int BUF_SIZE = 2048;
const int TIMEOUT = 10*1000;   // 超时时间为10s
enum TASK_STATUS {
    READY_TO_READ = 1,
    READY_TO_WRITE,
    SOMETHING_ERROR
};

enum IO_STATUS {
    READ_FINISH = 1,
    READ_ERROR,
    WRITE_FINISH,
    WRITE_AGAIN,
    WRITE_ERROR
};


class Echo: public BaseTask, public std::enable_shared_from_this<Echo>
{
    using SP_Timer = shared_ptr<TimerNode<Echo>>;
    using SP_Epoll = shared_ptr<Epoll<Echo>>;
    using SP_TimerManager = shared_ptr<TimerManager<Echo>>;
public:
    Echo(int sockfd, sockaddr_in addr): 
        BaseTask(sockfd, addr), timer_(nullptr), status(READY_TO_READ), m_buf{0}, m_read_index(0), m_bytes_have_send(0){}
    Echo() = delete;
    Echo(const Echo &) = delete;
    Echo &operator=(const Echo &) = delete;
    ~Echo() = default;

    static void Init(SP_TimerManager, SP_Epoll);
    void process() override;         // 业务逻辑
    void linkTimer(SP_Timer timer);
    void separateTimer();            // 与定时器单向解耦，即定时器删除时会连带删除任务
    void bilateralSeparateTimer();   // 与定时器双向解耦，即定时器删除时不会删除任务

private:
    SP_Timer timer_;
    static SP_TimerManager timer_manager_;  // 业务处理时需要添加计时器
    static SP_Epoll epoll_;   // 业务处理时需要修改监听的类别

private:
    int status;   // 状态机
    char m_buf[BUF_SIZE];
    int m_read_index;
    int m_bytes_have_send;
    int read_from_sock();
    int write_to_sock();
    void disconnection();
};

#endif