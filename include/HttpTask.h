// 处理HTTP的任务类
#ifndef _HTTPTASK_H
#define _HTTPTASK_H
#include "BaseTask.h"
#include "noncopyable.h"
#include "Logging.h"
#include <pthread.h>
#include <string>
#include <unordered_map>
using std::string;
using std::unordered_map;

// 使用pthread_once()
// 尽管所有变量和函数都是static，但不需要手动调用_init()
// 负责将后缀名转换成MIME类型
class MimeType: public noncopyable
{
public:
    static string getMime(const string &suffix);
    MimeType() = delete;

private:
    static pthread_once_t once_control_;
    static void _init();
    static unordered_map<string,string> mime;
};


class HttpTask: public BaseTask, public std::enable_shared_from_this<HttpTask>
{
    using SP_TimerManager = shared_ptr<TimerManager<HttpTask>>;
    using SP_Timer = shared_ptr<TimerNode<HttpTask>>;
    using SP_Epoll = shared_ptr<Epoll<HttpTask>>;

    // 主状态机的状态
    enum MainStatus {
        STATE_PARSE_REQUESTLINE = 0,
        STATE_PARSE_HEADERS,
        STATE_RECV_BODY,
        STATE_ANALYSIS,
        STATE_READY_TO_WRITE,
        STATE_FINISH,
        STATE_ERROR
    };

    enum RequestMethod {METHOD_GET = 0, METHOD_POST};
    enum HttpVersion {HTTP1_1 = 0, HTTP1_0};

public:
    HttpTask(int sock, sockaddr_in addr):
        BaseTask(sock, addr), 
        inBuf_(), 
        outBuf_(),
        main_status_(STATE_PARSE_REQUESTLINE),
        bytes_have_send_(0),
        timer_(nullptr),
        method_(METHOD_GET),
        file_name_(),
        httpVersion_(HTTP1_1),
        keep_alive_(false), 
        headers_() {}


    ~HttpTask();
    static void Init(SP_TimerManager, SP_Epoll);
    void linkTimer(SP_Timer timer);
    void separateTimer();
    void bilateralSeparateTimer();
    void process() override;

// 类静态数据
private:
    static SP_TimerManager timer_manager_;  // 业务处理时需要添加计时器
    static SP_Epoll epoll_;   // 业务处理时需要修改监听的类别

// 任务相关变量
private:   
    string inBuf_;          // 接收到的数据
    string outBuf_;         // 待发送的数据
    MainStatus main_status_;       // 主状态机
    int bytes_have_send_;   // 已经发送的字节数
    SP_Timer timer_;        // 定时器

// 解析到的信息
private:
    RequestMethod method_;        // 请求方式，GET或POST
    string file_name_;            // 请求的文件名
    HttpVersion httpVersion_;     // HTTP协议版本，1.0或1.1
    bool keep_alive_;             // 持续连接和非持续连接
    std::unordered_map<std::string, std::string> headers_;  // 首部行的键值对


// 私有函数
private:
    int _read();
    int _write();
    void _disconnect();
    void _reset();

    // 向发送缓存写入错误信息，并修改主状态机
    void _handleError(int err_num, const string &msg);

    // 维护连接，包括修改epoll事件、添加定时器，在process()的最后调用
    void _handleConnection();

    int _parse_requestline();
    int _parse_headers();
    int _recv_body();
    int _analysis_request();
};

#endif