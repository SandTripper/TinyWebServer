#include "mysql_connection_pool.h"

mysqlConnectionPool::mysqlConnectionPool()
{
    this->CurConn = 0;
    this->FreeConn = 0;
}

mysqlConnectionPool::~mysqlConnectionPool()
{
    DestroyPool();
}

//当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数
MYSQL *mysqlConnectionPool::GetConnection()
{
    MYSQL *con = NULL;

    if (0 == connList.size())
    {
        return NULL;
    }

    reserve.wait();

    lock.lock();

    con = connList.front();
    connList.pop_front();

    --FreeConn;
    ++CurConn;

    lock.unlock();
    return con;
}

//释放当前使用的连接
bool mysqlConnectionPool::ReleaseConnection(MYSQL *con)
{
    if (NULL == con)
    {
        return false;
    }

    lock.lock();

    connList.push_back(con);
    ++FreeConn;
    --CurConn;

    lock.unlock();

    reserve.post();

    return true;
}

int mysqlConnectionPool::GetFreeConn()
{
    return this->FreeConn;
}

//销毁数据库连接池
void mysqlConnectionPool::DestroyPool()
{
    lock.lock();
    if (!connList.empty())
    {
        list<MYSQL *>::iterator it;
        for (it = connList.begin(); it != connList.end(); ++it)
        {
            MYSQL *con = *it;
            mysql_close(con);
        }
        CurConn = 0;
        FreeConn = 0;
        connList.clear();
    }

    lock.unlock();
}

mysqlConnectionPool *mysqlConnectionPool::GetInstance()
{
    static mysqlConnectionPool connPool;
    return &connPool;
}

//构造初始化
void mysqlConnectionPool::init(string url, string User, string PassWord, string DataBaseName, int Port, unsigned int MaxConn)
{
    this->url = url;
    this->User = User;
    this->PassWord = PassWord;
    this->DatabaseName = DataBaseName;
    this->Port = Port;

    //操作连接池，加锁
    lock.lock();
    for (int i = 0; i < MaxConn; ++i)
    {
        MYSQL *con = NULL;
        con = mysql_init(con);

        if (con == NULL)
        {
            printf("Error: %s\n", mysql_error(con));
            exit(1);
        }
        con = mysql_real_connect(con, url.c_str(), User.c_str(), PassWord.c_str(),
                                 DataBaseName.c_str(), Port, NULL, 0);

        if (con == NULL)
        {
            printf("Error: %s\n", mysql_error(con));
            exit(1);
        }

        connList.push_back(con);
        ++FreeConn;
    }

    reserve = sem(FreeConn);

    this->MaxConn = FreeConn;

    lock.unlock();
}

mysql_connectionRAII::mysql_connectionRAII(MYSQL **con, mysqlConnectionPool *connPool)
{
    *con = connPool->GetConnection();

    conRAII = *con;
    poolRAII = connPool;
}

mysql_connectionRAII::~mysql_connectionRAII()
{
    poolRAII->ReleaseConnection(conRAII);
}
