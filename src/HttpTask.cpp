#include "HttpTask.h"
#include "Utils.h"
#include "Logging.h"
#include <errno.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>


// 持续连接的定时器是30s，非持续连接是2s
const int LONG_TIMEOUT = 30 * 1000;
const int SHORT_TIMEOUT = 2 * 1000;

const int PARSE_REQUESTLINE_FINISH = 0;
const int PARSE_REQUESTLINE_AGAIN = -1;
const int PARSE_REQUESTLINE_ERROR = -2;

const int PARSE_HEADER_FINISH = 0;
const int PARSE_HEADER_AGAIN = -1;
const int PARSE_HEADER_ERROR = -2;

const int RECV_BODY_FINISH = 0;
const int RECV_BODY_AGAIN = -1;
const int RECV_BODY_ERROR = -2;

const int ANALYSIS_FINISH = 0;
const int ANALYSIS_NOT_FOUND = -1;
const int ANALYSIS_BAD_REQUEST = -2;

const int WRITE_FINISH = 0;
const int WRITE_AGAIN = -1;
const int WRITE_ERROR = -2;

pthread_once_t MimeType::once_control_ = PTHREAD_ONCE_INIT;
unordered_map<string,string> MimeType::mime;
void MimeType::_init()
{
    mime[".html"]   = "text/html";
    mime[".avi"]    = "video/x-msvideo";
    mime[".bmp"]    = "image/bmp";
    mime[".c"]      = "text/plain";
    mime[".doc"]    = "application/msword";
    mime[".gif"]    = "image/gif";
    mime[".gz"]     = "application/x-gzip";
    mime[".htm"]    = "text/html";
    mime[".ico"]    = "application/x-ico";
    mime[".jpg"]    = "image/jpeg";
    mime[".png"]    = "image/png";
    mime[".txt"]    = "text/plain";
    mime[".mp3"]    = "audio/mp3";
    mime["default"] = "text/html";
}

string MimeType::getMime(const string &suffix)
{
    pthread_once(&once_control_, _init);
    if (mime.find(suffix) != mime.end())
        return mime[suffix];
    else
        return mime["default"];
}

shared_ptr<TimerManager<HttpTask>> HttpTask::timer_manager_(nullptr);
shared_ptr<Epoll<HttpTask>> HttpTask::epoll_(nullptr);

HttpTask::~HttpTask() 
{
    LOG_INFO << "disconnect with " << dotted_decimal_notation(addr_);
    close(sock_);
}

void HttpTask::Init(SP_TimerManager tm, SP_Epoll ep)
{
    timer_manager_ = tm;
    epoll_ = ep;
}

void HttpTask::linkTimer(SP_Timer timer)
{
    timer_ = timer;
}

void HttpTask::separateTimer()
{
    if (timer_)
    {
        timer_->setDeleted();
        timer_.reset();
    }
}

void HttpTask::bilateralSeparateTimer()
{
    if (timer_)
    {
        timer_->separate();
        timer_.reset();
    }
}


/*
任务处理逻辑：
    先判断是否可以发送数据，可以的话直接发送；否则先接收数据
    然后根据主状态机，执行相应函数
*/
void HttpTask::process()
{
    // 可以直接发送数据
    if (main_status_ == STATE_READY_TO_WRITE)
    {
        int ret = _write();
        if (ret == WRITE_FINISH)
        {
            main_status_ = STATE_FINISH;
            LOG_INFO << "Send message to " << dotted_decimal_notation(addr_) << " successful";
        }
        else if (ret == WRITE_AGAIN)
            main_status_ = STATE_READY_TO_WRITE;
        else
        {
            main_status_ = STATE_ERROR;
            LOG_ERROR << "Send message to " << dotted_decimal_notation(addr_) << " failed";
        }
    }

    // 否则要和定时器解耦并接受数据
    else
    {
        do {
            // 与定时器双向解耦
            bilateralSeparateTimer();

            // 读取套接字
            int read_len = _read();
            if (read_len < 0)
            {
                LOG_ERROR << "Bad Request from " << dotted_decimal_notation(addr_);
                _handleError(400, "Bad Request");
                break;
            }

            // 有请求出现但是读不到数据，可能是Request Aborted，或者来自网络的数据没有达到等原因
            // 最可能是对端已经关闭了，统一按照对端已经关闭处理
            else if (read_len == 0)
            {
                LOG_INFO << "Receive zero byte message from " << dotted_decimal_notation(addr_);
                main_status_ = STATE_ERROR;
                break;
            }

            // 根据主状态机进行处理
            if (main_status_ == STATE_PARSE_REQUESTLINE)
            {
                int ret = _parse_requestline();
                if (ret == PARSE_REQUESTLINE_FINISH)
                    main_status_ = STATE_PARSE_HEADERS;
                else if (ret == PARSE_REQUESTLINE_AGAIN)
                    break;
                else
                {
                    _handleError(400, "Bad Request");
                    break;
                }
            }
            if (main_status_ == STATE_PARSE_HEADERS)
            {
                int ret = _parse_headers();
                if (ret == PARSE_HEADER_FINISH)
                {
                    if (method_ == METHOD_GET)
                        main_status_ = STATE_ANALYSIS;
                    else if (method_ == METHOD_POST)
                        main_status_ = STATE_RECV_BODY;
                    else
                    {
                        _handleError(400, "Bad Request");
                        break;
                    }
                }
                else if (ret == PARSE_HEADER_AGAIN)
                    break;
                else
                {
                    _handleError(400, "Bad Request");
                    break;
                }
            }
            if (main_status_ == STATE_RECV_BODY)
            {
                int ret = _recv_body();
                if (ret == RECV_BODY_FINISH)
                    main_status_ = STATE_ANALYSIS;
                else if (ret == RECV_BODY_AGAIN)
                    break;
                else
                    _handleError(400, "Bad Request");
                    break;
            }
            if (main_status_ == STATE_ANALYSIS)
            {
                int ret = _analysis_request();
                if (ret == ANALYSIS_FINISH)
                {
                    main_status_ = STATE_READY_TO_WRITE;
                    if (method_ == METHOD_GET)
                        LOG_INFO << "Receive GET request successful";
                    else if (method_ == METHOD_POST)
                        LOG_INFO << "Receive POST request successful";
                }
                else if (ret == ANALYSIS_NOT_FOUND)
                {
                    _handleError(404, "Not Found");
                    break;
                }
                else
                    _handleError(400, "Bad Request");
                    break;
            }
        } while(false);
    }

    _handleConnection();
}


int HttpTask::_read()
{
    int read_len = 0;
    char buf[2048];
    while (1)
    {
        memset(buf, 0, sizeof(buf));
        int len = recv(sock_, buf, sizeof(buf), 0);
        if (len < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return read_len;
            else if (errno == EINTR)
                continue;
            else 
                return -1;
        }
        else if (len == 0)
            return read_len;
        else
        {
            inBuf_ += buf;
            read_len += len;
        }
    }
}

int HttpTask::_write()
{
    int write_len = 0;
    const char *ptr = outBuf_.c_str() + bytes_have_send_;
    while (write_len < outBuf_.size())
    {
        int len = send(sock_, ptr, outBuf_.size()-write_len, 0);
        if (len < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return WRITE_AGAIN;
            else if (errno == EINTR)
                continue;
            else
                return WRITE_ERROR;
        }
        write_len += len;
        ptr += len;
        bytes_have_send_ += len;
    }
    return WRITE_FINISH;
}

// 请求发生错误，向输出缓存中写入错误信息，更改主状态机为可写
void HttpTask::_handleError(int err_num, const string &msg)
{
    outBuf_.clear();
    std::string head, entity_body;

    entity_body += "<html><title>哎呀~出错了</title>";
    entity_body += "<body bgcolor=\"ffffff\">";
    entity_body += std::to_string(err_num) + " " + msg;
    entity_body += "<hr><em> Huanggomery's Web Server</em>\n</body></html>";

    head += "HTTP/1.1 " + std::to_string(err_num) + " " + msg + "\r\n";
    if (keep_alive_)
    {
        head += "Connection: keep-alive\r\n";
        head += "Keep-Alive: timeout=" + std::to_string(LONG_TIMEOUT/1000);
    }
    else
        head += "Connection: close\r\n";
    head += "Content-Length: " + std::to_string(entity_body.size()) + "\r\n";
    head += "Content-Type: text/html; charset=utf-8\r\n";
    head += "Date: " + get_gmt_time_str() + "\r\n";
    head += "Server: Huanggomery's Web Server\r\n";
    head += "\r\n";

    outBuf_ += head + entity_body;
    main_status_ = STATE_READY_TO_WRITE;

    LOG_INFO << "HTTP error " << std::to_string(err_num) << " " << msg << " from: " << dotted_decimal_notation(addr_);
}

// 根据主状态机进行最后处理，即维护定时器和epoll监听事件
void HttpTask::_handleConnection()
{
    if (main_status_ == STATE_READY_TO_WRITE)
        epoll_->epoll_mod(sock_, EPOLLOUT | EPOLLET | EPOLLONESHOT, shared_from_this());
    else if (main_status_ == STATE_ERROR)
        _disconnect();
    else if (main_status_ == STATE_FINISH)
    {
        if (keep_alive_)
        {
            _reset();
            timer_manager_->addTimer(shared_from_this(), LONG_TIMEOUT);
            epoll_->epoll_mod(sock_, EPOLLIN | EPOLLET | EPOLLONESHOT, shared_from_this());
        }
        else
            _disconnect();
    }
    else
    {
        int timeout = keep_alive_ ? LONG_TIMEOUT : SHORT_TIMEOUT;
        timer_manager_->addTimer(shared_from_this(), timeout);
        epoll_->epoll_mod(sock_, EPOLLIN | EPOLLET | EPOLLONESHOT, shared_from_this());
    }
}

int HttpTask::_parse_requestline()
{
    // 如果还没收到完整的请求行，则继续等待
    std::size_t end_pos = inBuf_.find("\r\n");
    if (end_pos == std::string::npos)
        return PARSE_REQUESTLINE_AGAIN;
    std::string request = inBuf_.substr(0, end_pos);
    if (inBuf_.size() > end_pos+2)
        inBuf_ = inBuf_.substr(end_pos+2);
    else
        inBuf_.clear();

    // 解析请求方式
    if (request.find("GET") == 0 && request[3] == ' ')
    {
        method_ = METHOD_GET;
        request = request.substr(4);
    }
    else if (request.find("POST") == 0 && request[4] == ' ')
    {
        method_ = METHOD_POST;
        request = request.substr(5);
    }
    else
        return PARSE_REQUESTLINE_ERROR;
    
    // 解析文件名
    if (request[0] != '/')
        return PARSE_REQUESTLINE_ERROR;
    std::size_t pos = request.find(" ");
    if (pos == std::string::npos)
        return PARSE_REQUESTLINE_ERROR;
    if (pos - 1 > 0)
        file_name_ = request.substr(1, pos-1);
    else
        file_name_ = "index.html";
    request = request.substr(pos+1);

    // 解析HTTP协议版本
    if (request.find("HTTP/1.0") != std::string::npos)
        httpVersion_ = HTTP1_0;
    else if (request.find("HTTP/1.1") != std::string::npos)
        httpVersion_ = HTTP1_1;
    else
        return PARSE_REQUESTLINE_ERROR;
    
    return PARSE_REQUESTLINE_FINISH;
}

int HttpTask::_parse_headers()
{
    std::size_t end_pos;
    while ((end_pos = inBuf_.find("\r\n")) != 0)
    {
        if (end_pos == std::string::npos)
            return PARSE_HEADER_AGAIN;

        // 找到冒号
        std::size_t pos = inBuf_.find(":");
        if (pos == std::string::npos || pos == 0)
            return PARSE_HEADER_ERROR;
        
        // 解析键值对
        std::string key = inBuf_.substr(0,pos);
        if (inBuf_[++pos] != ' ')
            return PARSE_HEADER_ERROR;
        if (inBuf_[++pos] == ' ')
            return PARSE_HEADER_ERROR;
        std::string value = inBuf_.substr(pos,end_pos-pos);
        headers_[key] = value;

        // 从缓存中删除该首部字段
        if (inBuf_.size() > end_pos+2)
            inBuf_ = inBuf_.substr(end_pos+2);
        else
            inBuf_.clear();
    }
    if (inBuf_.size() > 2)
        inBuf_ = inBuf_.substr(2);
    else
        inBuf_.clear();
    return PARSE_HEADER_FINISH;
}

int HttpTask::_recv_body()
{
    if (headers_.find("Content-Length") == headers_.end())
        return RECV_BODY_ERROR;
    int len = std::stoi(headers_["Content-Length"]);
    if (inBuf_.size() < len)
        return RECV_BODY_AGAIN;
    return RECV_BODY_FINISH;
}

int HttpTask::_analysis_request()
{
    std::string head = "HTTP/1.1 200 OK\r\n";
    std::string entity_body;
    outBuf_.clear();
    if (headers_.find("Connection") != headers_.end())
    {
        if (headers_["Connection"] == "keep-alive" || headers_["Connection"] == "Keep-Alive")
                keep_alive_ = true;
        else if (headers_["Connection"] == "close" || headers_["Connection"] == "Close")
            keep_alive_ = false;
    }
    if (keep_alive_)
    {
        head += "Connection: keep-alive\r\n";
        head += "Keep-Alive: timeout=" + std::to_string(LONG_TIMEOUT/1000);
    }
    else
        head += "Connection: close\r\n";

    // GET方式，需要根据文件名打开相应的文件，如果文件名是"hello"，就不需要
    if (method_ == METHOD_GET)
    {
        if (file_name_ == "hello" || file_name_ == "Hello")
        {
            entity_body = "Hello, I am Huanggomery's Web Server.";
            head += "Content-Length: " + std::to_string(entity_body.size()) + "\r\n";
            head += "Content-Type: " + MimeType::getMime(".txt") + "; charset=utf-8\r\n";
        }
        else
        {
            // 查看文件是否存在
            struct stat file_info;
            if (stat(file_name_.c_str(), &file_info) < 0)
                return ANALYSIS_NOT_FOUND;
            // 添加首部字段Content-Length
            head += "Content-Length: " + std::to_string(file_info.st_size) + "\r\n";

            // 获取文件格式，添加首部字段Content-Type
            auto dot_pos = file_name_.find(".");
            std::string content_type;
            if (dot_pos != std::string::npos)
                content_type = MimeType::getMime(file_name_.substr(dot_pos));
            else
                content_type = MimeType::getMime("default");
            head += "Content-Type: " + content_type + "; charset=utf-8\r\n";

            // 打开文件，并用mmap映射
            int filefd = open(file_name_.c_str(), O_RDONLY);
            char *file_ptr = static_cast<char *>(mmap(NULL, file_info.st_size, PROT_READ, MAP_PRIVATE, filefd, 0));
            close(filefd);
            if (file_ptr == nullptr)
            {
                munmap(file_ptr, file_info.st_size);
                return ANALYSIS_NOT_FOUND;
            }

            entity_body = string(file_ptr, file_info.st_size);
            munmap(file_ptr, file_info.st_size);
        }
    }

    // POST方式，实现实体主体部分大小写转换就行
    else if (method_ == METHOD_POST)
    {
        // 添加首部字段Content-Length和Content-Type
        head += "Content-Length: " + headers_["Content-Length"] + "\r\n";
        head += "Content-Type: " + MimeType::getMime(".txt") + "; charset=utf-8\r\n";

        // 大小写转换，并存储到entity_body中
        for (char c : inBuf_)
        {
            if (c >= 'a' && c <= 'z')
                entity_body.push_back(c - 'a' + 'A');
            else if (c >= 'A' && c <= 'Z')
                entity_body.push_back(c - 'A' + 'a');
            else
                entity_body.push_back(c);
        }
    }

    // 添加首部字段Date，使用GMT时间
    head += "Date: " + get_gmt_time_str() + "\r\n";
    // 添加首部字段Server
    head += "Server: Huanggomery's Web Server\r\n";
    // 首部字段结束，回车换行
    head += "\r\n";
    // 填充outBuf_
    outBuf_ += head + entity_body;

    return ANALYSIS_FINISH;
}

void HttpTask::_reset()
{
    inBuf_.clear();
    outBuf_.clear();
    main_status_ = STATE_PARSE_REQUESTLINE;
    bytes_have_send_ = 0;
    file_name_.clear();
    headers_.clear();
}

void HttpTask::_disconnect()
{
    bilateralSeparateTimer();
    epoll_->epoll_del(sock_);
    // 此时计时器对象和fd2task中的指针都被清空了，
    // 但是还有工作线程的run()函数中还有最后一个指针，所以对象暂时还不会被析构
    // 一旦process()执行完，run()中最后一个指针析构，该对象也随之析构
}