TinyWebServer
===============

Linux下C++轻量级Web服务器

功能
---------------
* 响应GET请求，返回指定文件
* 通过时间轮定时器处理非活动连接
* 可分别设置listenfd的触发模式和connfd的触发模式

TODO
---------------
* 响应POST请求，用于实现账号注册，登录
  

BUG
---------------
* webbranch 压测基本都是fail(已解决)

记录
---------------
* 在《Linux高性能服务器编程》源代码的main函数中，以下两行在实际运行中需要注释掉
  ```cpp
  struct linger tmp = {1, 0};
  setsockopt(listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
  ```
  这两行代码是为了调试方便，但是会使得webbench压测全是fail，
  并且在客户端选择Connection:close时，客户端无法接收大文件

* main.cpp内的listenfd触发模式和http_conn.cpp内的listenfd触发模式需要一致
  不可以一个文件define listenfdET，另一个文件define listenfdLT
  这样会出现不可预知的错误

致谢
---------------
《Linux高性能服务器编程》，游双著.

项目来源:[TinyWebServer](https://github.com/qinguoyi/TinyWebServer/)

