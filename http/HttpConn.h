/***************************************
 * 线程池的模板参数类
 * 用以封装对逻辑任务的处理
 * HttpConn
 * *************************************/
#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

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
#include "locker.h"
class HttpConn
{
    public:
    static const int FILENAME_LEN = 200; // 文件名的最大长度
    static const int READ_BUFFER_SIZE = 2048; // 读缓冲区的大小
    static const int WRITE_BUFFER_SIZE = 1024; // 写缓冲区的大小

    // HTTP 请求方法，这里我们先仅支持GET
    enum METHOD {GET= 0,
                 POST,
                 HEAD,
                 PUT,
                 DELETE,
                 TRACE,
                 OPTIONS,
                 CONNECT,
                 PATCH
    };

    // 解析客户请求时，主状态机所处的状态
    enum CHECK_STATE {CHECK_STATE_REQUESTLINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };

    // 服务器处理HTTP请求的可能结果
    enum HTTP_CODE{ NO_REQUEST, 
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURCE,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNECTION  
    };

    // 行的读取状态
    enum LINE_STATUS { LINE_OK,
        LINE_BAD,
        LINE_OPEN
    };

    public:
        void Init(int sockfd, const sockaddr_in& addr); // 初始化新接受的连接
        void CloseConn(bool real_close = true); // 关闭连接
        void Process(); // 处理客户请求
        bool Read();  // 非阻塞读操作
        bool Write(); // 非阻塞写操作
    private:
        /* 初始化连接 */ 
        void Init(); 
        /* 解析HTTP请求 */
        HTTP_CODE ProcessRead(); 
        /* 填充HTTP应答 */ 
        bool ProcessWrite(HTTP_CODE ret); 
        
        /* 下面一组函数被 ProcessRead 调用以分析HTTP请求 */
        HTTP_CODE ParseRequestLine(char* text);
        HTTP_CODE ParseHeaders(char* text);
        HTTP_CODE ParseContent(char* text);
        HTTP_CODE DoRequest();
        char* GetLine(){ return m_read_buf + m_start_line; };
        LINE_STATUS ParseLine();

        /* 下面这一组函数被 ProcessWrite 调用来填充HTTP应答 */
        void Unmap();
        bool AddResponse(const char* format, ...);
        bool AddContent( const char* content );
        bool AddStatusLine( int status, const char* title);
        bool AddHeaders( int content_length);
        bool AddLinger();
        bool AddBlankLine();
    public:
         /* 所有socket上的事件都被注册到同一个epoll内核事件表中，
            所以将epoll文件描述符设置为静态的 */
        static int m_epollfd;
        /* 统计用户数量 */
        static int m_counts;
    private:
        int  m_sockfd; // 该HTTP连接的socket
        sockaddr_in m_address; // 对方的socket地址

        char m_read_buf[READ_BUFFER_SIZE];// 读缓冲区
        int m_readIdx; // 标识读缓冲区中已经读入的客户数据的最后一个字节的下一个位置
        int m_checkedIdx; // 当前正在分析的字符在读缓冲区中的位置
        int m_start_line; // 当前正在解析的行的起始位置

        char m_write_buf[WRITE_BUFFER_SIZE]; // 写缓冲区
        int m_writeIdx; // 写缓冲区中待发送的字节数

        /* 主状态机当前所处的状态 */
        CHECK_STATE m_check_state;

        METHOD m_method; // 请求方法

        /* 客户请求的目标文件的完整路径，其内容等于doc_root + m_url， 
        doc_root是网站的根目录 */
        char m_read_file[FILENAME_LEN];
        char* m_url; // 客户请求的目标的文件名称
        char* m_version; // HTTP协议版本号， 我们仅支持HTTP/1.1
        char* m_host; // 主机名
        int m_content_length; // HTTP请求的消息体的长度
        bool m_linger; // HTTP 请求是否要求保持连接

        /* 客户请求的目标文件被 mmap 映射到内存中的起始位置 */
        char* m_file_address;
        
        /* 目标文件的状态，通过它我们可以判断文件是否存在、是否为目录、是否可读
        ，并获取文件大小等信息 
        */
        struct stat m_file_stat;
        
        /* 我们将采用 writev 来执行写操作，所以定义下面两个成员，
           其中 m_iv_count 表示被写内存块的数量
        */
        int m_iv_count;
        struct iovec m_iv[2];
};
#endif //HTTPCONNECTION_H