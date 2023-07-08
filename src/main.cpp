#include <iostream>
#include <getopt.h>
#include "Task.h"
#include "WebServer.h"

const int THREAD_NUM = 15;
const int PORT = 9190;

int main(int argc, char** argv)
{
    int thread_num = THREAD_NUM, port = PORT;
    // 先解析参数
    int opt;
    const char *str = "t:p:";
    while ((opt = getopt(argc, argv, str)) != -1)
    {
        switch (opt)
        {
        case 't':
            thread_num = atoi(optarg);
            break;
        case 'p':
            port = atoi(optarg);
            break;
        default:
            break;
        }
    }

    auto server = WebServer<Echo>::CreateWebServer(port, 10*1000, thread_num, 10000);  // 端口号、初始超时时间、线程数、工作队列长度
    if (server)
        server->work();
    
    return 0;
}
