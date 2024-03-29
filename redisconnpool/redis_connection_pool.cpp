#include "redis_connection_pool.h"
#include <string>

#include "../log/log.h"

using namespace std;

locker Redis::m_lock;

Redis::Redis()
{
}

Redis::~Redis()
{
    this->m_connect = NULL;
    this->m_reply = NULL;
}

bool Redis::connect(string host, int port, string password)
{
    m_host = host;
    m_port = port;
    m_password = password;
    this->m_connect = redisConnect(host.c_str(), port);
    if (this->m_connect != NULL && this->m_connect->err)
    {
        printf("connect error: %s\n", this->m_connect->errstr);
        return 0;
    }

    this->m_reply = (redisReply *)redisCommand(this->m_connect, "AUTH %s", password.c_str());
    std::string str = this->m_reply->str;
    freeReplyObject(this->m_reply);
    return true;
}

std::string Redis::get(std::string key)
{
    m_lock.lock();
    this->m_reply = (redisReply *)redisCommand(this->m_connect, "GET %s", key.c_str());
    m_lock.unlock();
    std::string str = this->m_reply->str;
    freeReplyObject(this->m_reply);
    return str;
}

void Redis::set(std::string key, std::string value)
{
    m_lock.lock();
    redisCommand(this->m_connect, "SET %s %s", key.c_str(), value.c_str());
    m_lock.unlock();
}

void Redis::select(int db_id)
{
    freeReplyObject(redisCommand(this->m_connect, "SELECT %d", db_id));
}

pair<int, string> Redis::query(string content, int db_id)
{
    int status;
    m_lock.lock();
    select(db_id);
    this->m_reply = (redisReply *)redisCommand(this->m_connect, content.c_str());

    m_lock.unlock();
    string str;
    switch (m_reply->type)
    {
    case REDIS_REPLY_STRING:
        status = 1;
        str = this->m_reply->str;
        break;
    case REDIS_REPLY_INTEGER:
        status = 2;
        str = to_string(this->m_reply->integer);
        break;

    case REDIS_REPLY_STATUS:
        status = 1;
        str = this->m_reply->str;
        break;
    case REDIS_REPLY_NIL:
        status = 0;
        str = "";
        break;
    case REDIS_REPLY_ERROR:
        status = -1;
        str = this->m_reply->str;
        printf("fuck\n");
        fflush(stdout);
        LOG_ERROR("redis error :%s", this->m_reply->str);
        redisFree(this->m_connect);
        connect(m_host, m_port, m_password);
        break;
    default:
        status = 3;
        str = "ARRAY";
        break;
    }
    freeReplyObject(this->m_reply);
    return make_pair(status, str);
}

void Redis::close()
{
    redisFree(this->m_connect);
}

redisConnectionPool::redisConnectionPool::redisConnectionPool()
{
    this->m_cur_conn = 0;
    this->m_free_conn = 0;
}

redisConnectionPool::redisConnectionPool::~redisConnectionPool()
{
    DestroyPool();
}

// 当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数
Redis *redisConnectionPool::redisConnectionPool::GetConnection()
{
    Redis *con = NULL;

    if (0 == m_conn_list.size())
    {
        return NULL;
    }

    m_reserve.wait();

    m_lock.lock();

    con = m_conn_list.front();
    m_conn_list.pop_front();

    --m_free_conn;
    ++m_cur_conn;

    m_lock.unlock();
    return con;
}

// 释放当前使用的连接
bool redisConnectionPool::redisConnectionPool::ReleaseConnection(Redis *con)
{
    if (NULL == con)
    {
        return false;
    }

    m_lock.lock();

    m_conn_list.push_back(con);
    ++m_free_conn;
    --m_cur_conn;

    m_lock.unlock();

    m_reserve.post();

    return true;
}

int redisConnectionPool::redisConnectionPool::GetFreeConn()
{
    return this->m_free_conn;
}

// 销毁数据库连接池
void redisConnectionPool::redisConnectionPool::DestroyPool()
{
    m_lock.lock();
    if (!m_conn_list.empty())
    {
        list<Redis *>::iterator it;
        for (it = m_conn_list.begin(); it != m_conn_list.end(); ++it)
        {
            Redis *con = *it;
            con->close();
        }
        m_cur_conn = 0;
        m_free_conn = 0;
        m_conn_list.clear();
    }

    m_lock.unlock();
}

// 构造初始化
void redisConnectionPool::redisConnectionPool::init(string url, int Port, string PassWord, unsigned int MaxConn)
{
    this->m_host = url;
    this->m_password = PassWord;
    this->m_port = Port;

    // 操作连接池，加锁
    m_lock.lock();
    for (int i = 0; i < MaxConn; ++i)
    {
        Redis *con = new Redis;

        if (!con->connect(url.c_str(), Port, PassWord.c_str()))
        {
            printf("Redis connect Error: %s\n");
            exit(1);
        }

        m_conn_list.push_back(con);
        ++m_free_conn;
    }

    m_reserve = sem(m_free_conn);

    this->m_max_conn = m_free_conn;

    m_lock.unlock();
}

redis_connectionRAII::redis_connectionRAII(Redis **con, redisConnectionPool *connPool)
{
    *con = connPool->GetConnection();

    m_conRAII = *con;
    m_poolRAII = connPool;
}

redis_connectionRAII::~redis_connectionRAII()
{
    m_poolRAII->ReleaseConnection(m_conRAII);
}
