# C++ Linux High Performance Web Server



## Introduction

本项目是一个Web服务器，参考游双老师和陈硕老师的书。使用Reactor模型，能解析GET和POST请求，支持长短连接，使用异步日志。



## Environment

+ OS: Ubuntu 20.04
+ Compiler:  g++ 9.4.0



## Build

```shell
./build.sh
```



## Usage

```shell
cd build
sudo ./HttpServer [-p port] [-t thread_numbers]
```



## 浏览器测试

使用浏览器，地址输入http://127.0.0.1

即可得到如下页面

<img src="./image/WebServerTest.png" style="zoom:80%;" />	

输入不存在的URL，会得到404 NOT FOUND

![](./image/notfound.png)	

## 代码统计

![](./image/代码统计.png)	
