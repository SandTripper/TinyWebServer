#ifndef REDIS_CONNECTION_POOL_H
#define REDIS_CONNECTION_POOL_H

#include <string>
#include <list>
#include <cstdio>
#include <cstdlib>
#include <hiredis/hiredis.h>

#include "../locker/locker.h"

#define USER_DB 1
#define SESSION_DB 2

class Redis
{
public:
    Redis();

    ~Redis();

    bool connect(std::string host, int port, std::string password = "");

    std::string get(std::string key);

    void set(std::string key, std::string value);

    std::pair<int, std::string> query(std::string content, int db_id);

    void close();

private:
    void select(int db_id);

    redisContext *m_connect;
    redisReply *m_reply;

    static locker m_lock;

    std::string m_host;
    int m_port;
    std::string m_password;
};

class redisConnectionPool
{
public:
    redisConnectionPool();

    ~redisConnectionPool();

    // 获取数据库连接
    Redis *GetConnection();
    // 释放连接
    bool ReleaseConnection(Redis *conn);
    // 获取连接
    int GetFreeConn();
    // 销毁数据库连接池
    void DestroyPool();

    void init(std::string url, int Port, std::string PassWord, unsigned int MaxConn);

private:
    // 最大连接数
    unsigned int m_max_conn;
    // 当前已使用的连接数
    unsigned int m_cur_conn;
    // 当前空闲的连接数
    unsigned int m_free_conn;

private:
    // 主机地址
    std::string m_host;
    // 数据库端口号
    std::string m_port;
    // 登录数据库密码
    std::string m_password;

private:
    locker m_lock;

    std::list<Redis *> m_conn_list; // 连接池

    sem m_reserve;
};

// RAII类
class redis_connectionRAII
{
public:
    redis_connectionRAII(Redis **con, redisConnectionPool *connPool);

    ~redis_connectionRAII();

private:
    Redis *m_conRAII;
    redisConnectionPool *m_poolRAII;
};

#endif