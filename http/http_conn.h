#ifndef HTTP_CONN_H
#define HTTP_CONN_H

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>

#include "../locker/locker.h"
#include "../mysqlconnpool/mysql_connection_pool.h"
#include "../redisconnpool/redis_connection_pool.h"
#include "../log/log.h"

class http_conn
{
public:
    //文件名的最大长度
    static const int FILENAME_LEN = 200;
    //读缓冲区的大小
    static const int READ_BUFFER_SIZE = 2048;
    //写缓冲区的大小
    static const int WRITE_BUFFER_SIZE = 1024;
    // HTTP请求方法
    enum METHOD
    {
        GET = 0,
        POST = 1,
        HEAD = 2,
        PUT = 3,
        DELETE = 4,
        TRACE = 5,
        OPTIONS = 6,
        CONNECT = 7,
        PATCH = 8
    };

    //主状态机的三种可能状态，分别表示：当前正在分析请求行，当前正在分析头部字段，当前正在分析内容
    enum CHECK_STATE
    {
        CHECK_STATE_TYPELINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };

    //行的读取状态，分别表示：读取到一个完整的行，行出错，行数据尚且不完整
    enum LINE_STATUS
    {
        LINE_OK = 0,
        LINE_BAD,
        LINE_OPEN
    };

    /*服务器处理HTTP请求的结果：
    NO_REQUEST表示请求不完整，需要继续读取客户数据：
    GET_REQUEST表示获得了一个完整的的客户请求；
    BAD_REQUSET表示客户请求有语法错误；
    NO_RESOURCE表示没有该文件；
    FORBIDDEN_REQUEST表示客户对资源没有足够的访问权限；
    FILE_REQUEST_MEM表示请求的文件已经就绪,通过加载文件到内存发送；
    FILE_REQUEST_MAP表示请求的文件已经就绪,通过内存映射发送；
    REDIRECT表示需要重定向；
    INTERNAL_ERROR表示服务器内部错误；
    CLOSED_CONNECTION表示客户端已经关闭连接*/
    enum HTTP_CODE
    {
        NO_REQUEST,
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURCE,
        FORBIDDEN_REQUEST,
        FILE_REQUEST_MEM,
        FILE_REQUEST_MAP,
        REDIRECT,
        INTERNAL_ERROR,
        CLOSED_CONNECTION
    };

public:
    http_conn();

    ~http_conn();

public:
    //初始化新接受的链接
    void init(int sockfd, const sockaddr_in &addr, int listenfd_Trig_mode, int connfd_Trig_mode);

    //关闭连接
    void close_conn(bool real_close = true);

    //处理客户请求
    void process();

    //非阻塞读操作
    bool read();

    //非阻塞写操作
    bool write();

    //返回客户的地址
    sockaddr_in *get_address();

    //初始化数据库，读出到map
    void init_cache();

private:
    //初始化连接
    void init();

    //解析HTTP请求
    HTTP_CODE process_read();

    //填充HTTP应答
    bool process_write(HTTP_CODE ret);

    //下面这一组函数被process_read调用以分析HTTP请求
    HTTP_CODE parse_type_line(char *text);
    HTTP_CODE parse_headers(char *text);
    HTTP_CODE parse_content(char *text);
    HTTP_CODE do_request();
    char *get_line();
    LINE_STATUS parse_line();

    std::string extractSessionID(const std::string &content);

    //下面这一组函数被process_write调用以填充HTTP应答
    void unmap();
    void unmem();
    bool add_response(const char *format, ...);
    bool add_content(const char *content);
    bool add_status_line(int status, const char *title);
    bool add_headers(int content_length);
    bool add_content_length(int content_length);
    bool add_set_cookie(const char *cookie);
    bool add_location(const char *location);
    bool add_linger();
    bool add_content_type(const char *content_type);
    bool add_blank_line();

public:
    /*所有socket上的事件都被注册到同一个epoll内核事件表中,
    所以将epoll文件描述符设置为静态的*/
    static int m_epollfd;

    //统计用户数量
    static int m_user_count;

    //指向全局唯一mysql连接池实例的指针
    static mysqlConnectionPool *m_mysql_conn_pool;

    //指向全局唯一读redis连接池实例的指针
    static redisConnectionPool *m_read_redis_conn_pool;

    //指向全局唯一写redis连接池实例的指针
    static redisConnectionPool *m_write_redis_conn_pool;

private:
    //该HTTP连接的socket和对方的socket地址
    int m_sockfd;
    sockaddr_in m_address;

    //读缓冲区
    char m_read_buf[READ_BUFFER_SIZE];

    //标识读缓冲中已经读入的客户数据的最后一个字节的下一个位置
    int m_read_idx;

    //当前正在分析的字符在读缓冲区中的位置
    int m_checked_idx;

    //当前正在解析的行的起始位置
    int m_start_line;

    //写缓冲区
    char m_write_buf[WRITE_BUFFER_SIZE];

    //写缓冲区中待发送的字节数
    int m_write_idx;

    //主状态机当前所处的状态
    CHECK_STATE m_check_state;

    //请求方法
    METHOD m_method;

    //客户请求的目标文件的完整路径，其内容等于doc_root+m_url,doc_root是网站根目录
    char m_real_file[FILENAME_LEN];

    //客户请求的的目标文件的文件名
    char *m_url;

    // HTTP协议版本号，我们仅支持HTTP/1.1
    char *m_version;

    //主机名
    char *m_host;

    // HTTP请求的消息体的长度
    int m_content_length;

    // HTTP请求是否要保持连接
    bool m_linger;

    //客户请求的目标文件被mmap到内存中的起始位置
    char *m_file_address;

    //指向要发送的内容
    char *m_write_mem;

    // m_write_mem的大小
    int m_write_mem_len;

    /*目标文件的状态，通过它我们可以判断文件是否存在，
    是否为目录，是否可读，并获取文件大小等信息*/
    struct stat m_file_stat;

    /*我们采用writev来执行写操作，所以定义下面两个成员，
     其中m_iv_count表示被写内存块的数量*/
    struct iovec m_iv[2];
    int m_iv_count;
    //需要发送的字节数
    int m_bytes_to_send;
    //已经发送的字节数
    int m_bytes_have_send;
    //是否启用的POST
    int m_cgi;
    //存储请求头数据
    char *m_content;

    // listenfd是否开启ET模式，ET模式为1，LT模式为0
    int m_listenfd_Trig_mode;

    // connfd是否开启ET模式，ET模式为1，LT模式为0
    int m_connfd_Trig_mode;

    // HTTP请求的cookie字段
    std::string m_cookie;

    //该会话的sessionID
    std::string m_session_id;

    //缓存欢迎界面的内容
    static char *welcome_html;
    //欢迎界面的大小
    static int welcome_html_len;

    int m_want_set_cookie;
    string m_cookie_to_set;

    //重定向的网址
    string m_location;
};

#endif