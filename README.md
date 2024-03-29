TinyWebServer
===============

Linux下C++轻量级Web服务器

功能
---------------
* 响应GET和POST请求，实现账号的注册和登录
* 通过时间轮定时器处理非活动连接
* 可对 listen 和 connect 分别选择 ET 和 LT 模式
* 可选择同步记录日志和异步记录日志
* 可通过配置文件修改启动参数
* 使用cookie和session结合保存登录信息

参数设置
---------------
配置文件：server.config

参数：

* localhost = 主机地址
* port = 端口号
* connection_mode = LT模式写 0，ET 模式写 1
* listen_mode = LT模式写 0，ET 模式写 1
* log_mode = 同步写日志填 0，异步写日志填 1
* threadnum = 线程数
  
压力测试
---------------
测试软件：[Web Bench](http://home.tiscali.cz/~cz210552/webbench.html)

测试环境：腾讯云2核4G服务器

测试参数：
* 用户数量：5000 
* 并发请求持续时间：60s

测试结果：

LT+LT
QPS：16550

![](./ReadmeResource/LT+LT-test.png)

LT+ET
QPS：16404

![](./ReadmeResource/LT+ET-test.png)

ET+LT
QPS：12013

![](./ReadmeResource/ET+LT-test.png)

ET+ET
QPS：17547

![](./ReadmeResource/ET+ET-test.png)



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



致谢
---------------
《Linux高性能服务器编程》，游双著.

项目来源:[TinyWebServer](https://github.com/qinguoyi/TinyWebServer/)

