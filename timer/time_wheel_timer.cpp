#include "time_wheel_timer.h"

tw_timer::tw_timer(int rot, int ts)
    : next(NULL), prev(NULL), rotation(rot), time_slot(ts)
{
}

time_wheel::time_wheel() : cur_slot(0)
{
    for (int i = 0; i < N; ++i)
    {
        slots[i] = NULL; //初始化每个槽的头节点
    }
}

time_wheel::~time_wheel()
{
    //遍历槽，销毁其中的定时器
    for (int i = 0; i < N; ++i)
    {
        tw_timer *tmp = slots[i];
        while (tmp)
        {
            slots[i] = tmp->next;
            delete tmp;
            tmp = slots[i];
        }
    }
}

tw_timer *time_wheel::add_timer(int timeout)
{
    if (timeout < 0)
    {
        return NULL;
    }

    int ticks = 0; //记录还要转多少次

    //如果timeout不满一个间隔SI，则向上取整为1，否则向下取整

    if (timeout < SI)
    {
        ticks = 1;
    }
    else
    {
        ticks = timeout / SI;
    }

    //计算待插入的定时器转动多少圈后触发
    int rot = ticks / N;

    //计算待插入的定时器应该存放在哪个槽
    int ts = (cur_slot + (ticks % N)) % N;

    //创建新的定时器
    tw_timer *timer = new tw_timer(rot, ts);

    //如果第ts个槽没有定时器，则设置timer为该槽的头节点
    if (!slots[ts])
    {
        slots[ts] = timer;
    }
    //否则将timer插入第ts槽的链表
    else
    {
        timer->next = slots[ts];
        slots[ts]->prev = timer;
        slots[ts] = timer;
    }

    return timer;
}

void time_wheel::del_timer(tw_timer *timer)
{
    if (!timer)
    {
        return;
    }

    int ts = timer->time_slot;

    //如果目标定时器就是第ts槽的头节点，则重新设置头节点后再删除
    if (slots[ts] == timer)
    {
        slots[ts] = timer->next;
        if (slots[ts])
        {
            slots[ts]->prev = NULL;
        }
        delete timer;
    }
    //否则直接连接timer在链表的前后节点后再删除
    else
    {
        timer->prev->next = timer->next;
        if (timer->next)
        {
            timer->next->prev = timer->prev;
        }
        delete timer;
    }
}

void time_wheel::tick()
{
    tw_timer *tmp = slots[cur_slot]; //取得当前槽的头节点

    while (tmp) //遍历链表
    {
        //如果当前遍历的定时器的rotation的值大于0，说明还没到定的时间
        if (tmp->rotation > 0)
        {
            tmp->rotation--;
            tmp = tmp->next;
        }
        //否则，说明定时器到期，执行定时任务，然后删除该定时器
        else
        {
            tmp->cb_func(tmp->user_data);
            //如果tmp就是该槽的头节点，则重新设置头节点后再删除
            if (tmp == slots[cur_slot])
            {
                slots[cur_slot] = tmp->next;
                delete tmp;
                if (slots[cur_slot])
                {
                    slots[cur_slot]->prev = NULL;
                }
                tmp = slots[cur_slot];
            }
            //否则直接连接tmp在链表的前后节点后再删除
            else
            {
                tmp->prev->next = tmp->next;
                if (tmp->next)
                {
                    tmp->next->prev = tmp->prev;
                }
                tw_timer *tmp2 = tmp->next;
                delete tmp;
                tmp = tmp2;
            }
        }
    }
    cur_slot = (cur_slot + 1) % N; //更新当前的槽
}

int time_wheel::get_SI()
{
    return SI;
}

Utils::Utils(int timeslot)
{
    m_TIMESLOT = timeslot;
}

int Utils::setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option; //返回原来的状态，方便恢复
}

void Utils::addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;

    if (TRIGMode == 1)
    {
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    }
    else
    {
        event.events = EPOLLIN | EPOLLRDHUP;
    }

    if (one_shot)
    {
        event.events |= EPOLLONESHOT;
    }

    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

void Utils::sig_handler(int sig)
{
    //为保证函数的可重入性，保留原来的errno
    int save_errno = errno;
    int msg = sig;
    send(u_pipefd[1], (char *)&msg, 1, 0); //将信号值写入管道，以通知主循环
    errno = save_errno;
}

void Utils::addsig(int sig, void(handler)(int), bool restart)
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if (restart)
    {
        sa.sa_flags |= SA_RESTART;
    }
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

void Utils::timer_handler()
{
    m_time_wheel.tick();
    alarm(m_time_wheel.get_SI());
}

void Utils::show_error(int connfd, const char *info)
{
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

void cb_func(client_data *user_data)
{
    epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);
    close(user_data->sockfd);
    http_conn::m_user_count--;
}
