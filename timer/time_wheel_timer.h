#ifndef TIME_WHEEL_TIMER_H
#define TIME_WHEEL_TIMER_H

#include <netinet/in.h>
#include <stdio.h>
#include <time.h>
#include <sys/fcntl.h>
#include <sys/epoll.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

#include "../http/http_conn.h"

class tw_timer; //前向声明

//绑定socket和定时器
struct client_data
{
    sockaddr_in address;
    int sockfd;
    tw_timer *timer;
};

//定时器类

class tw_timer
{
public:
    tw_timer(int rot, int ts);

public:
    //记录定时器在时间轮转多少圈之后生效
    int rotation;

    //记录定时器属于时间轮的哪个槽
    int time_slot;

    //回调函数
    void (*cb_func)(client_data *);

    //客户数据
    client_data *user_data;

    //指向下一个定时器
    tw_timer *next;

    //指向前一个定时器
    tw_timer *prev;
};

//时间轮类

class time_wheel
{
public:
    time_wheel();

    ~time_wheel();

    //根据定时值timeout创建一个新的定时器，把他插入合适的槽中，并返回指向定时器内存的指针
    tw_timer *add_timer(int timeout);

    //删除目标定时器timer
    void del_timer(tw_timer *timer);

    // SI时间到后，调用该函数，时间轮向前转一下
    void tick();

    //获取转动一次经过的时间
    int get_SI();

private:
    //时间轮上槽的数目
    static const int N = 60;

    //转动一次经过的时间
    static const int SI = 1;

    //时间轮的槽，每个元素指向一个定时器链表
    tw_timer *slots[N];

    //时间轮的当前槽
    int cur_slot;
};

class Utils
{
public:
    Utils(int timeslot);

    //对文件描述符设置非阻塞
    int setnonblocking(int fd);

    //将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
    void addfd(int epollfd, int fd, bool one_shot, int TRIGMode);

    //信号处理函数
    static void sig_handler(int sig);

    //设置信号函数
    void addsig(int sig, void(handler)(int), bool restart = true);

    //定时处理任务，重新定时以不断触发SIGALRM信号
    void timer_handler();

    void show_error(int connfd, const char *info);

public:
    //信号处理函数用来通知主循环的管道
    static int u_pipefd[2];

    //时间轮定时器
    static time_wheel m_time_wheel;

    // epoll描述符
    static int u_epollfd;

    int m_TIMESLOT;
};

//回调函数
void cb_func(client_data *user_data);

#endif