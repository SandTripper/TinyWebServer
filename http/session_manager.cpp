#include "session_manager.h"

#include <chrono>

#include "md5.h"
#include "../log/log.h"

using namespace std;

// murmur函数的种子
static const uint SEED = 114514;

BloomFilter SessionManager::m_bloom_filter_session_id(10);

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
    MD5 md5;
    string session_id = to_string(getMillisecondTimeStamp()) += to_string(md5.murmur3(user_name.c_str(), user_name.length(), SEED)) + to_string(rand());

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

    m_bloom_filter_session_id.addKey(session_id);

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
        return Session("", 0, "");
    }

    if (!m_bloom_filter_session_id.hasKey(session_id)) // 不在布隆过滤器
    {
        return Session("", 0, "");
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

long long SessionManager::getMillisecondTimeStamp()
{
    long long timems = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    return timems;
}

// 初始化布隆过滤器
void SessionManager::init_bloom_filter()
{
    // 从连接池中取一个连接
    MYSQL *mysql = NULL;
    mysql_connectionRAII mysqlcon(&mysql, m_mysql_conn_pool);

    // 在user表中检索username，passwd数据，浏览器端输入
    if (mysql_query(mysql, "SELECT * FROM session_tb"))
    {
        LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));
    }

    // 从表中检索完整的结果集
    MYSQL_RES *result = mysql_store_result(mysql);

    // 返回结果集中的列数
    int num_fields = mysql_num_fields(result);

    // 返回所有字段结构的数组
    MYSQL_FIELD *fields = mysql_fetch_fields(result);

    // 从结果集中获取下一行，将对应的用户名和密码，存入map中
    while (MYSQL_ROW row = mysql_fetch_row(result))
    {
        string temp1(row[0]);
        m_bloom_filter_session_id.addKey(temp1);
    }
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
