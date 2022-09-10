#ifndef SQL_CONNECTION_POOL_H
#define SQL_CONNECTION_POOL_H

#include <string>
#include <list>
#include <mysql/mysql.h>
#include <cstdio>
#include <cstdlib>

#include "../locker/locker.h"

using namespace std;

class connection_pool
{
public:
    connection_pool();

    ~connection_pool();

    //获取数据库连接
    MYSQL *GetConnection();
    //释放连接
    bool ReleaseConnection(MYSQL *conn);
    //获取连接
    int GetFreeConn();
    //销毁数据库连接池
    void DestroyPool();

    //单例懒汉模式
    static connection_pool *GetInstance();

    void init(string url, string User, string PassWord, string DataBaseName, int Port, unsigned int MaxConn);

private:
    //最大连接数
    unsigned int MaxConn;
    //当前已使用的连接数
    unsigned int CurConn;
    //当前空闲的连接数
    unsigned int FreeConn;

private:
    //主机地址
    string url;
    //数据库端口号
    string Port;
    //登录数据库用户名
    string User;
    //登录数据库密码
    string PassWord;
    //使用数据库名
    string DatabaseName;

private:
    locker lock;

    list<MYSQL *> connList; //连接池

    sem reserve;
};

// RAII类
class connectionRAII
{
public:
    connectionRAII(MYSQL **con, connection_pool *connPool);

    ~connectionRAII();

private:
    MYSQL *conRAII;
    connection_pool *poolRAII;
};

#endif