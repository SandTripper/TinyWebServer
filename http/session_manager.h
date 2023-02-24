#ifndef SESSION_MANAGER_H
#define SESSION_MANAGER_H

#include <string>

#include "../mysqlconnpool/mysql_connection_pool.h"
#include "../redisconnpool/redis_connection_pool.h"
#include "bloom_filter.h"
#include "../locker/locker.h"

class Session;

class SessionManager
{
public:
    // 获取全局唯一实例
    static SessionManager *getInstance();

    ~SessionManager();

    // 新建一个session会话并返回会话ID
    std::string addSession(std::string user_name, int survival_time = 2592000);

    Session getSession(std::string session_id);

    void delSession(std::string session_id);

    // 初始化布隆过滤器
    void init_bloom_filter();

public:
    // 指向全局唯一mysql连接池实例的指针
    mysqlConnectionPool *m_mysql_conn_pool;

    // 指向全局唯一读redis连接池实例的指针
    redisConnectionPool *m_read_redis_conn_pool;

    // 指向全局唯一写redis连接池实例的指针
    redisConnectionPool *m_write_redis_conn_pool;

private:
    // 获取毫秒级时间戳
    long long getMillisecondTimeStamp();

    SessionManager();

    // sessionID的布隆过滤器
    static BloomFilter m_bloom_filter_session_id;

    static locker m_lock;
};

class Session
{
public:
    Session(std::string session_id = "", uint death_time = 4294967295, std::string user_name = "");

    ~Session();

    std::string getSessionID();

    uint getDeathTime();

    std::string getUserName();

private:
    std::string m_session_id;

    uint m_death_time;

    std::string m_user_name;
};

#endif