#include "HttpConn.h"

/* 定义HTTP响应的一些状态信息 */
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_from = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internet Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";
/* 网站的根目录*/
const char* doc_root = "/var/wwww/html";

int setNonBlocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

void addfd(int epollfd, int fd, bool one_shot)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    if(one_shot) {
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setNonBlocking(fd);
}

void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

void modfd(int epollfd, int fd, int ev)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

int HttpConn::m_counts = 0;
int HttpConn::m_epollfd = -1;

// 初始化新接受的连接
void HttpConn::Init(int sockfd, const sockaddr_in& addr)
{
    m_sockfd = sockfd;
    m_address = addr;
    
    /* 如下两行是为了避免 TIME_WAIT 状态， 仅用于调试，实际使用时应该去掉 */
    int reuse = -1;
    setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    addfd(m_epollfd, sockfd, true);
    m_counts++;

    Init();
}
void HttpConn::Init()
{
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checkedIdx = 0;
    m_readIdx = 0;
    m_writeIdx = 0;
    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
}

// 关闭连接
void HttpConn::CloseConn(bool real_close = true)
{
    if( real_close && (m_sockfd != -1))
    {
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_counts--; // 关闭一个连接时，将客户总量减一
    }
}

/* 处理客户请求，由线程池中的工作线程调用，这里处理HTTP请求的入口函数*/ 
void HttpConn::Process()
{
    HTTP_CODE readRet = ProcessRead();
    if( readRet == NO_REQUEST ) {
       modfd( m_epollfd, m_sockfd, EPOLLIN);
       return;
    }
   
    bool writeRet = ProcessWrite(readRet); 
    if( !writeRet ) {
       CloseConn();
    }
    
    // 注册数据可写事件
    modfd(m_epollfd, m_sockfd, EPOLLOUT);
}

HttpConn::HTTP_CODE HttpConn::ProcessRead()
{
    /* 主状态机 */
    LINE_STATUS lineStatus = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char* text = 0;

    while( 
        ( ( m_check_state == CHECK_STATE_CONTENT ) 
            && ( lineStatus == LINE_OK ) )
        || ( ( lineStatus = ParseLine() ) == LINE_OK) )
    {
        text = GetLine();
        m_start_line = m_checkedIdx;
        printf("got 1 http line: %s\n", text);

        switch (m_check_state)
        {
            case CHECK_STATE_REQUESTLINE:
            {
                ret = ParseRequestLine(text);
                if( ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                }
                break;  
            }
            case CHECK_STATE_HEADER:
            {
                ret = ParseHeaders(text);
                if( ret == BAD_REQUEST ) {
                    return BAD_REQUEST;
                } 
                else if ( ret == GET_REQUEST )
                {
                    return DoRequest();
                }
                break; 
            }
            case CHECK_STATE_CONTENT:
            {
                ret = ParseContent(text);
                if( ret == GET_REQUEST ) {
                    return DoRequest();
                }
                lineStatus = LINE_OPEN;
                break;
            }
            default:
            {
                return INTERNAL_ERROR;
            }   
        }
    }
    return NO_REQUEST;
}

HttpConn::HTTP_CODE HttpConn::ParseRequestLine(char* text)
{
   // 解析 HTTP请求行，获得请求方法、目标URL，以及HTTP版本号
   m_url = strpbrk( text, "\t");
   if(! m_url ) {
      return BAD_REQUEST;
   }

   *m_url++ = '\0'; // ‘\0’ 是表示NULL字符

   char* method = text;
   if(strcasecmp(method, "GET") == 0) {
       m_mthod = GET; 
   } else {
       // 这里其实还有其他请求类型，此处暂时以 GET实现
       return BAD_REQUEST;
   }

   m_url += strspn( m_url, "\t");
   m_version = strpbk(m_url, "\t");
   if (strcasecmp(m_version, "HTTP/1.1") != 0) {
       return BAD_REQUEST;
   }

   if(strcasecmp(m_url, "http://", 7) == 0) {
      m_url += 7;
      m_url = strchr(m_url, '/');
   }

   if( !m_url || m_url[0] != '/' ) {
       return BAD_REQUEST;
   }

   m_check_state = CHECK_STATE_HEADER;
   return NO_REQUEST;
}

HttpConn::HTTP_CODE HttpConn::ParseHeaders(char* text)
{
    // 解析http请求的头部信息
    // 遇到空行，表示头部字段解析完毕
    if( text[ 0 ] == '\0' ) {
        /* 如果HTTP请求有消息体， 则还需要读取m_content_length字节的消息体，
        状态机转移到 CHECK_STATE_CONTENT 状态*/
        if( m_content_length != 0 ) {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }

        // 否则说明取得到了一个完整的HTTP请求
        return GET_REQUEST;
    } else if(strcasecmp( text, "Connection:", 11 )) { // 处理 Connection 头部字段
        text += 11;
        text += strspn(text, "\t");
        if( strcasecmp( text, "keep-alive" ) ==0 ) {
            m_linger = true;
        }
    } else if( strcasecmp( text, "Content-Length:", 15) == 0) { // 处理 Content-Length 头部字段
        text += 15;
        text += strspn(text, "\t");
        m_content_length = atol( text);
    } else if(strcasecmp(text, "Host:", 5) ==0 ) {
        text += 5;
        text += strspn( text, "\t");
        m_host = text;
    } else {
         printf("oop!, unknow header %s\n", text);
    }
    return NO_REQUEST;
}

HttpConn::HTTP_CODE HttpConn::ParseContent(char* text)
{
    // 这个函数没有真正解析HTTP请求的消息体， 只是判断它是否被完整地读入了
    if( m_readIdx >= (m_content_length + m_checkedIdx)) {
        text[m_content_length] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

/* 当得到一个完整、正确的HTTP请求时，我们就分析目标文件的属性，
   如果目标文集存在、对所有用户可读，且不是目录，则使用mmap将其映射到内存地址 m_file_address 处
   ，并告诉调用者获取文件成功
 */
HttpConn::HTTP_CODE HttpConn::DoRequest()
{
    stcpy( m_real_file, doc_root );
    int len = strlen(doc_root);
    strncpy(m_real_file + len, m_url, FILENAME - len -1);
    if( stat(m_real_file, &m_file_stat) <0 ) {
        return NO_RESOURCE;
    }

    if(! (m_file_stat.st_mode & S_IROTH)) {
        return FORBIDDEN_REQUEST;
    }

    if( S_ISDIR(m_file_stat.st_mode) ) {
        return BAD_REQUEST;
    }

    int fd = open(m_real_file, O_RDONLY);
    m_file_address = (char*)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE,fd ,0);
    close(fd);
    return FILE_REQUEST;
}
/* 对内存映射区执行unmap操作,解除文件到内存的映射 */
void HttpConn::Unmap()
{
    if(m_file_address) {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

// 非阻塞读操作, 循环读取客户数据，直到无数可读或者对方关闭连接
bool HttpConn::Read() 
{
    if( m_readIdx >= READ_BUFFER_SIZE) {
        return false;
    }

    int bytes_read = 0;
    while( true ) {
        bytes_read = recv(m_sockfd, m_read_buf+m_readIdx, READ_BUFFER_SIZE-m_readIdx, 0);

        if(bytes_read == -1) {
            if( errno == EAGAIN || errno == EWOULDBLOCK ) {
                break;
            }
            return false;
        }
        else if( bytes_read == 0 ) {
            return false;
        }

        m_readIdx+=bytes_read;
    }
    return true;
}

// 非阻塞写操作
void HttpConn::Write()
{

}

/* 根据服务器处理 HTTP 请求的结果， 决定返回给客户端的内容 */
bool HttpConn::ProcessWrite(HTTP_CODE ret)
{
    switch( ret ) 
    {
        case INTERNAL_ERROR:
        {
            
            break;
        }
    }
}