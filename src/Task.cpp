#include "Task.h"
#include "Logging.h"
#include <cstring>

shared_ptr<TimerManager<Echo>> Echo::timer_manager_(nullptr);
shared_ptr<Epoll<Echo>> Echo::epoll_(nullptr);

void Echo::Init(SP_TimerManager tm, SP_Epoll ep)
{
    timer_manager_ = tm;
    epoll_ = ep;
}

void Echo::linkTimer(SP_Timer timer)
{
    timer_ = timer;
}

void Echo::separateTimer()
{
    if (timer_)
    {
        timer_->setDeleted();
        timer_.reset();
    }
}

void Echo::bilateralSeparateTimer()
{
    if (timer_)
    {
        timer_->separate();
        timer_.reset();
    }
}

void Echo::process()
{
    if (status == READY_TO_READ)
    {
        // 先和计时器双向解耦
        bilateralSeparateTimer();
        // 从sock读取数据
        int ret = read_from_sock();
        if (ret == READ_ERROR)
        {
            LOG_ERROR << "read error, close socket " << sock_ << ", " << inet_ntoa(addr_.sin_addr);
            status = SOMETHING_ERROR;
            disconnection();
            return;
        }
        LOG_INFO << "receive message from " << inet_ntoa(addr_.sin_addr) <<" : " << m_buf;
        
        // 进行大小写变换
        for (int i = 0; i < m_read_index; ++i)
        {
            if (m_buf[i] >= 'a' && m_buf[i] <= 'z')
                m_buf[i] += ('A'-'a');
            else if (m_buf[i] >= 'A' && m_buf[i] <= 'Z')
                m_buf[i] += ('a'-'A');
        }
        // 更改状态机
        status = READY_TO_WRITE;
        // 修改epoll中注册的事件为等待写
        epoll_->epoll_mod(sock_, EPOLLOUT | EPOLLET | EPOLLONESHOT, shared_from_this());
    }
    else if (status == READY_TO_WRITE)
    {
        // 向sock发送数据
        int ret = write_to_sock();
        if (ret == WRITE_AGAIN)
        {
            epoll_->epoll_mod(sock_, EPOLLOUT | EPOLLET | EPOLLONESHOT, shared_from_this());
            return;
        }
        else if (ret == WRITE_ERROR)
        {
            LOG_ERROR << "write error, close socket " << sock_ << ", " << inet_ntoa(addr_.sin_addr);
            status = SOMETHING_ERROR;
            disconnection();
            return;
        }
        LOG_INFO << "send message to " << inet_ntoa(addr_.sin_addr) << " successful";
        // 清空数据缓存
        memset(m_buf, 0, BUF_SIZE);
        m_read_index = 0;
        m_bytes_have_send = 0;
        // 更改状态机
        status = READY_TO_READ;
        // 重新添加计时器
        timer_manager_->addTimer(shared_from_this(), TIMEOUT);
        // 修改epoll中注册的事件为等待读
        epoll_->epoll_mod(sock_, EPOLLIN | EPOLLET | EPOLLONESHOT, shared_from_this());
    }
    else {}
}

int Echo::read_from_sock()
{
    int str_len;
    while (1)
    {
        str_len = recv(sock_, m_buf+m_read_index, BUF_SIZE-m_read_index, 0);
        if (str_len < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                if (m_buf[m_read_index-1] == '\n')
                    m_buf[m_read_index-1] = '\0';
                return READ_FINISH;
            }
            else
                return READ_ERROR;
        }
        if (str_len == 0)
            return READ_ERROR;
        else
            m_read_index += str_len;
    }
}

int Echo::write_to_sock()
{
    if (m_buf[m_read_index-1] == '\0')
        m_buf[m_read_index-1] = '\n';
    int str_len;
    while (m_bytes_have_send < m_read_index)
    {
        str_len = send(sock_, m_buf+m_bytes_have_send, strlen(m_buf+m_bytes_have_send), 0);
        if (str_len < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK || EPOLLONESHOT)
                return WRITE_AGAIN;
            return WRITE_ERROR;
        }
        else if (str_len == 0)
            return WRITE_ERROR;
        else
            m_bytes_have_send += str_len;
    }
    return WRITE_FINISH;
}

void Echo::disconnection()
{
    bilateralSeparateTimer();
    close(sock_);
    epoll_->epoll_del(sock_);
    /*  
        此时计时器对象和fd2task中的指针都被清空了，
        但是还有工作线程的run()函数中还有最后一个指针，所以对象暂时还不会被析构
    */
}