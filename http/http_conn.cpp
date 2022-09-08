#include "http_conn.h"
#include <fstream>

//#define connfdET //边缘触发非阻塞
#define connfdLT //水平触发阻塞

//#define listenfdET //边缘触发非阻塞
#define listenfdLT //水平触发阻塞

//定义HTTP响应的一些状态考虑
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file from this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the requested file.\n";

//网站的根目录
const char *doc_root = "/var/www/html";

//将文件描述符设置为非阻塞
int setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option; //返回原来的状态以便恢复
}

//将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void addfd(int epollfd, int fd, bool oneshot)
{
    epoll_event event;
    event.data.fd = fd;
#ifdef connfdET
    event.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
#endif

#ifdef connfdLT
    event.events = EPOLLIN | EPOLLRDHUP;
#endif

#ifdef listenfdET
    event.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
#endif

#ifdef listenfdLT
    event.events = EPOLLIN | EPOLLRDHUP;
#endif

    if (oneshot)
    {
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

//删除事件
void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

//将事件重置为EPOLLONESHOT
void modfd(int epollfd, int fd, int ev)
{
    epoll_event event;
    event.data.fd = fd;

#ifdef connfdET
    event.events = ev | EPOLLONESHOT | EPOLLRDHUP | EPOLLET;
#endif

#ifdef connfdLT
    event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
#endif

    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

http_conn::http_conn()
{
}

http_conn::~http_conn()
{
}

void http_conn::init()
{
    bytes_have_send = 0;
    bytes_to_send = 0;
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
}

void http_conn::close_conn(bool real_close)
{
    if (real_close && (m_sockfd != -1))
    {
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--; //关闭一个连接时，将客户总量减1
    }
}

void http_conn::process()
{
    HTTP_CODE read_ret = process_read();
    if (read_ret == NO_REQUEST)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }

    bool write_ret = process_write(read_ret);
    if (!write_ret)
    {
        close_conn();
    }
    modfd(m_epollfd, m_sockfd, EPOLLOUT);
}

bool http_conn::read()
{
    if (m_read_idx >= READ_BUFFER_SIZE)
    {
        return false;
    }
    int bytes_read = 0;

#ifdef connfdLT
    bytes_read = recv(m_sockfd, m_read_buf + m_read_idx,
                      READ_BUFFER_SIZE - m_read_idx, 0);
    m_read_idx += bytes_read;
    if (bytes_read <= 0)
    {
        return false;
    }
    else
    {
        return true;
    }
#endif

#ifdef connfdET
    while (true)
    {
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx,
                          READ_BUFFER_SIZE - m_read_idx, 0);
        if (bytes_read == -1)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                break;
            }
            else
            {
                return false;
            }
        }
        else if (bytes_read == 0)
        {
            return false;
        }

        m_read_idx += bytes_read;
    }
    return true;
#endif
}

bool http_conn::write()
{

    int temp = 0;

    if (bytes_to_send == 0)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        init();
        return true;
    }

    while (1)
    {
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if (temp < 0)
        {
            /*如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件，
            虽然在此期间，服务器无法立即接收到同一客户的下一个请求，
            但这可以保证链接的完整性*/
            if (errno == EAGAIN)
            {
                modfd(m_epollfd, m_sockfd, EPOLLOUT);
                return true;
            }
            else
            {
                unmap();
                return false;
            }
        }
        bytes_to_send -= temp;
        bytes_have_send += temp;

        if (bytes_have_send >= m_iv[0].iov_len)
        {
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }
        else
        {
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
        }

        if (bytes_to_send <= 0)
        {
            // MDEBUG
            // if (m_iv_count == 2)
            // {
            //     printf("%s", m_iv->iov_base);
            //     // printf("%s", (m_iv + 1)->iov_base);
            // }

            //发送HTTP响应成功，根据HTTP请求中的Connnection字段决定是否立即关闭连接
            unmap();
            modfd(m_epollfd, m_sockfd, EPOLLIN);
            if (m_linger)
            {
                init();
                return true;
            }
            else
            {
                return false;
            }
        }
    }
}

http_conn::HTTP_CODE http_conn::process_read()
{
    LINE_STATUS linestatus = LINE_OK; //记录当前行的读取状态
    HTTP_CODE retcode = NO_REQUEST;   //记录HTTP请求的处理结果
    char *text = 0;

    //主状态机，用于从buffer中取出所有完整的行
    while (((m_check_state == CHECK_STATE_CONTENT) && (linestatus == LINE_OK)) ||
           ((linestatus = parse_line()) == LINE_OK))
    {
        text = get_line();            // start_line是行在buffer中 的起始位置
        m_start_line = m_checked_idx; //记录下一行的起始位置
        printf("%s\n", text);
        fflush(stdout);

        switch (m_check_state)
        {
        case CHECK_STATE_REQUESTLINE: //第一个状态，分析请求行
            retcode = parse_request_line(text);
            if (retcode == BAD_REQUEST)
            {
                return BAD_REQUEST;
            }
            break;
        case CHECK_STATE_HEADER: //第二个状态，分析头部字段
            retcode = parse_headers(text);
            if (retcode == BAD_REQUEST)
            {
                return BAD_REQUEST;
            }
            else if (retcode == GET_REQUEST)
            {
                return do_request();
            }
            break;
        case CHECK_STATE_CONTENT:
            retcode = parse_content(text);
            if (retcode == GET_REQUEST)
            {
                return do_request();
            }
            linestatus = LINE_OPEN;
            break;
        default:
            return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}

//根据服务器HTTP请求的结果，决定返回给客户端的内容
bool http_conn::process_write(HTTP_CODE ret)
{
    //("process_write %d\n", ret);
    switch (ret)
    {
    case INTERNAL_ERROR:
        add_status_line(500, error_500_title);
        add_headers(strlen(error_500_form));
        if (!add_content(error_500_form))
        {
            return false;
        }
        break;
    case BAD_REQUEST:
        add_status_line(400, error_400_form);
        add_headers(strlen(error_400_form));
        if (!add_content(error_400_form))
        {
            return false;
        }
        break;
    case NO_RESOURCE:
        add_status_line(404, error_404_title);
        add_headers(strlen(error_404_form));
        if (!add_content(error_404_form))
        {
            return false;
        }
        break;
    case FORBIDDEN_REQUEST:
        add_status_line(403, error_403_title);
        add_headers(strlen(error_403_form));
        if (!add_content(error_403_form))
        {
            return false;
        }
        break;
    case FILE_REQUEST:
        add_status_line(200, ok_200_title);
        if (m_file_stat.st_size != 0)
        {
            add_headers(m_file_stat.st_size);
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_idx;
            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            bytes_to_send = m_write_idx + m_file_stat.st_size;
            return true;
        }
        else
        {
            const char *ok_string = "<html><body></body></html>";
            add_headers(strlen(ok_string));
            if (!add_content(ok_string))
                return false;
        }
        break;
    default:
        return false;
    }

    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}

//解析HTTP请求行，获得请求方法，目标URL，以及HTTP版本号
http_conn::HTTP_CODE http_conn::parse_request_line(char *text)
{
    m_url = strpbrk(text, " \t");
    //如果请求行中没有空白字符或'\t'字符，则HTTP请求必有问题
    if (!m_url)
    {
        return BAD_REQUEST;
    }
    *m_url++ = '\0';

    char *method = text;
    if (strcasecmp(method, "GET") == 0) //仅支持GET方法
    {
        m_method = GET;
    }
    else
    {
        return BAD_REQUEST;
    }

    m_url += strspn(m_url, " \t");
    m_version = strpbrk(m_url, " \t");
    if (!m_version)
    {
        return BAD_REQUEST;
    }
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");
    //仅支持HTTP/1.1/
    if (strcasecmp(m_version, "HTTP/1.1") != 0)
    {
        return BAD_REQUEST;
    }

    //检查URL是否合法
    if (strncasecmp(m_version, "http://", 7) == 0)
    {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }
    if (strncasecmp(m_url, "https://", 8) == 0)
    {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }

    // URL非法
    if (!m_url || m_url[0] != '/')
    {
        return BAD_REQUEST;
    }

    //当url为/时，显示主页
    if (strlen(m_url) == 1)
    {
        strcat(m_url, "index.html");
    }

    // HTTP请求行处理完毕，状态转移到头部字段的分析
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_headers(char *text)
{
    //遇到空行，表示头部字段解析完毕
    if (text[0] == '\0')
    {
        /*如果HTTP请求有消息体，则还需要读取m_content_length字节的消息体，
        状态机转移到CHECK_STATE_CONTENT状态*/
        if (m_content_length != 0)
        {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        //否则说明我们得到了一个完整的HTTP请求
        else
        {
            return GET_REQUEST;
        }
    }
    //处理"Connection"头部字段
    else if (strncasecmp(text, "Connection:", 11) == 0)
    {
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0)
        {
            m_linger = true;
        }
    }
    //处理"Content-Length"头部字段
    else if (strncasecmp(text, "Content-Length:", 15) == 0)
    {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    }
    //处理"Host"头部字段
    else if (strncasecmp(text, "Host:", 5) == 0)
    {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    else //其他头部字段都不处理
    {
        // printf("I can not handle this header:%s\n", text);
    }

    return NO_REQUEST;
}

//我们没有真正解析HTTP请求的消息体，只是判断它是否被完整读入了
http_conn::HTTP_CODE http_conn::parse_content(char *text)
{
    if (m_read_idx >= (m_content_length + m_checked_idx))
    {
        text[m_content_length] = '\0';
        return GET_REQUEST;
    }
    else
    {
        return NO_REQUEST;
    }
}

/*当得到一个完整的、正确的HTTP请求时，我们就分析目标文件的属性，如果目标文件存在，
对所有用户可读，且不是目录，则使用mmap将其映射到内存地址m_file_address处，
并告诉调用者获取文件成功*/
http_conn::HTTP_CODE http_conn::do_request()
{

    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);

    strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);

    // puts(m_real_file);

    if (stat(m_real_file, &m_file_stat) < 0)
    {
        return NO_RESOURCE;
    }
    if (!(m_file_stat.st_mode & S_IROTH))
    {
        return FORBIDDEN_REQUEST;
    }
    if (S_ISDIR(m_file_stat.st_mode))
    {
        return BAD_REQUEST;
    }
    int fd = open(m_real_file, O_RDONLY);
    m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

char *http_conn::get_line()
{
    return m_read_buf + m_start_line;
}

http_conn::LINE_STATUS http_conn::parse_line()
{
    char temp;
    /*buffer中第0~checked_index字节都已分析完毕，
    第checked_index~(read_index-1)字节由下面的循环挨个分析*/
    for (; m_checked_idx < m_read_idx; ++m_checked_idx)
    {
        //获得当前要分析的字节
        temp = m_read_buf[m_checked_idx];
        //如果当前的字节是'\r'，即回车符，则说明可能读取到一个完整的行
        if (temp == '\r')
        {
            /*如果'\r'字符碰巧是目前buffer中最后一个已经被读入的客户数据，
            那么这次分析没有读取到一个完整的行，返回LINE_OPEN以表示还需要
            继续读取客户数据才能进一步分析*/
            if ((m_checked_idx + 1) == m_read_idx)
            {
                return LINE_OPEN;
            }
            //如果下一个字符是'\n'，则说明我们成功读取到一个完整的行
            else if (m_read_buf[m_checked_idx + 1] == '\n')
            {
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            //否则，就说明客户发送的HTTP请求存在语法问题
            return LINE_BAD;
        }
        //如果当前的字节是'\n'，即换行符，则也说明可能读取到一个完整的行
        else if (temp == '\n')
        {
            //如果上一个字符是'\r'，则说明我们成功读取到一个完整的行
            if ((m_checked_idx > 1) && m_read_buf[m_checked_idx - 1] == '\r')
            {
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            //否则，就说明客户发送的HTTP请求存在语法问题
            else
            {
                return LINE_BAD;
            }
        }
    }
    return LINE_OPEN;
}

//对内存映射区执行munmap操作
void http_conn::unmap()
{
    if (m_file_address)
    {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

//往写缓冲区写入待发送的数据
bool http_conn::add_response(const char *format, ...)
{
    if (m_write_idx >= WRITE_BUFFER_SIZE)
    {
        return false;
    }
    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(m_write_buf + m_write_idx,
                        WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
    if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx))
    {
        va_end(arg_list);
        return false;
    }
    m_write_idx += len;
    va_end(arg_list);
    return true;
}

bool http_conn::add_content(const char *content)
{
    return add_response("%s", content);
}

bool http_conn::add_status_line(int status, const char *title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

bool http_conn::add_headers(int content_length)
{
    add_content_length(content_length);
    add_linger();

    // MDEBUG
    add_content_type("text/html");

    add_blank_line();
}

bool http_conn::add_content_length(int content_length)
{
    // MDEBUG
    // return add_response("Transfer-Encoding: chunked\r\n");
    return add_response("Content-Length: %d\r\n", content_length);
}

bool http_conn::add_linger()
{
    return add_response("Connection: %s\r\n", (m_linger == true) ? "keep-alive" : "close");
}

bool http_conn::add_blank_line()
{
    return add_response("%s", "\r\n");
}

bool http_conn::add_content_type(const char *content_type)
{
    return add_response("Content-Type: %s\r\n", content_type);
}

void http_conn::init(int sockfd, const sockaddr_in &addr)
{
    m_sockfd = sockfd;
    m_address = addr;
    //如下两行是为了避免TIMEWAIT状态，仅用于调试，实际使用时应该去掉
    // int reuse = 1;
    // setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    addfd(m_epollfd, sockfd, true);
    m_user_count++;

    init();
}
