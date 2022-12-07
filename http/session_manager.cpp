#include "session_manager.h"

#include <chrono>

#include "md5.h"
#include "../log/log.h"

using namespace std;

static const uint SEED = 114514;

SessionManager *SessionManager::getInstance()
{
    static SessionManager session_manager;
    return &session_manager;
}

SessionManager::~SessionManager()
{
}

string SessionManager::addSession(string user_name, int survival_time)
{
    string session_id = to_string(getMillisecondTimeStamp()) += to_string(hash(user_name.c_str(), user_name.length())) + to_string(rand());
    MD5 md5;
    session_id = md5.encode(session_id);
    uint death_time = 0;
    if (survival_time != -1)
    {
        death_time = getMillisecondTimeStamp() / 1000 + survival_time;
    }
    else
    {
        death_time = 4294967295;
    }

    string query_str;

    Redis *write_redis;
    redis_connectionRAII rediscon(&write_redis, m_write_redis_conn_pool);
    write_redis->select(SESSION_DB);
    query_str = (string) "SET " + session_id + " " + user_name;
    write_redis->query(query_str);
    if (survival_time != -1)
    {
        query_str = "EXPIRE " + session_id + " " + to_string(survival_time);
        write_redis->query(query_str).c_str();
    }

    MYSQL *mysql;
    mysql_connectionRAII mysqlcon(&mysql, m_mysql_conn_pool);
    query_str = (string) "INSERT INTO session_tb VALUES('" + session_id + "', '" + user_name + "', " + to_string(death_time) + ")";
    if (mysql_query(mysql, query_str.c_str()))
        ;
    {
        LOG_ERROR("INSERT error:%s\n", mysql_error(mysql));
    }

    return session_id;
}

Session SessionManager::getSession(string session_id)
{
    bool is_session_exist;
    string user_name = "";
    uint death_time = 0;

    if (session_id == "")
    {
        return Session(session_id, death_time, user_name);
    }

    string query_str;

    // 从redis缓存查找该用户名
    Redis *read_redis;
    redis_connectionRAII(&read_redis, m_read_redis_conn_pool);
    read_redis->select(SESSION_DB);
    query_str = (string) "EXISTS " + session_id;

    if (read_redis->query(query_str) == "0") // redis缓存中没有该用户名
    {
        MYSQL *mysql;
        mysql_connectionRAII mysqlcon(&mysql, m_mysql_conn_pool);
        query_str = (string) "SELECT * from session_tb WHERE session_id = '" + session_id + "'";
        if (mysql_query(mysql, query_str.c_str()))
        {
            LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));
        }

        // 从表中检索完整的结果集
        MYSQL_RES *result = mysql_store_result(mysql);

        if (MYSQL_ROW row = mysql_fetch_row(result)) // 存在该session
        {
            user_name = string(row[1]);
            death_time = stoll(string(row[2]));
            if (death_time >= getMillisecondTimeStamp() / 1000)
            {
                is_session_exist = true;
            }
            else
            {
                is_session_exist = false;
            }
        }
        else
        {
            is_session_exist = false;
        }
    }
    else
    {
        is_session_exist = true;

        query_str = (string) "GET " + session_id;
        user_name = read_redis->query(query_str);
        query_str = (string) "TTL " + session_id;
        int survival_time = stoi(read_redis->query(query_str));
        if (survival_time != -1)
        {
            death_time = getMillisecondTimeStamp() / 1000 + survival_time;
        }
        else
        {
            death_time = 4294967295;
        }
    }
    if (is_session_exist)
        return Session(session_id, death_time, user_name);
    else
        return Session("", 0, "");
}

void SessionManager::delSession(string session_id)
{
    string query_str;

    Redis *write_redis;
    redis_connectionRAII rediscon(&write_redis, m_write_redis_conn_pool);
    write_redis->select(SESSION_DB);
    query_str = (string) "DEL " + session_id;
    write_redis->query(query_str);

    MYSQL *mysql;
    mysql_connectionRAII mysqlcon(&mysql, m_mysql_conn_pool);
    query_str = (string) "DELETE FROM session_tb WHERE session_id = '" + session_id + "'";
    if (mysql_query(mysql, query_str.c_str()))
    {
        LOG_ERROR("DELETE error:%s\n", mysql_error(mysql));
    }
}

uint SessionManager::hash(const char *key, size_t len)
{
    uint32_t h = SEED;
    if (len > 3)
    {
        const uint32_t *key_x4 = (const uint32_t *)key;
        size_t i = len >> 2;
        do
        {
            uint32_t k = *key_x4++;
            k *= 0xcc9e2d51;
            k = (k << 15) | (k >> 17);
            k *= 0x1b873593;
            h ^= k;
            h = (h << 13) | (h >> 19);
            h = (h * 5) + 0xe6546b64;
        } while (--i);
        key = (const char *)key_x4;
    }
    if (len & 3)
    {
        size_t i = len & 3;
        uint32_t k = 0;
        key = &key[i - 1];
        do
        {
            k <<= 8;
            k |= *key--;
        } while (--i);
        k *= 0xcc9e2d51;
        k = (k << 15) | (k >> 17);
        k *= 0x1b873593;
        h ^= k;
    }
    h ^= len;
    h ^= h >> 16;
    h *= 0x85ebca6b;
    h ^= h >> 13;
    h *= 0xc2b2ae35;
    h ^= h >> 16;
    return h;
}

long long SessionManager::getMillisecondTimeStamp()
{
    long long timems = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    return timems;
}

SessionManager::SessionManager()
{
}

Session::Session(string session_id, uint death_time, string user_name) : m_session_id(session_id), m_death_time(death_time), m_user_name(user_name)
{
}

Session::~Session()
{
}

string Session::getSessionID()
{
    return m_session_id;
}

uint Session::getDeathTime()
{
    return m_death_time;
}

string Session::getUserName()
{
    return m_user_name;
}
