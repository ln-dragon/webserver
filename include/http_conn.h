#ifndef HTTP_CONN_H
#define HTTP_CONN_H
#include <sys/epoll.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
 #include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <sys/uio.h>
#include "LFUCache.h"

class http_conn{
public:
    http_conn();
    ~http_conn();
    static int m_epollfd;//所有的socket上的事件都被注册到同一个epoll中
    static int m_user_count;//统计用户数量

    static const int FILENAME_LEN = 200;// 文件名的最大长度
    static const int READ_BUFFER_SIZE = 2048;
    static const int WRITE_BUFFER_SIZE = 2048;
    enum METHOD
    {
        GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT, PATH
    };
    //主状态机的状态
    enum CHECK_STATE
    {
        CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT
    };
    //解析报文的结果
    enum HTTP_CODE
    {
        NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, 
        FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION
    };
    //从状态机的状态  
    enum LINE_STATUS
    {
        LINE_OK = 0, LINE_BAD, LINE_OPEN
    };

    void process();//处理客户端的请求
    bool process_write(HTTP_CODE ret);//填充HTTP应答
    HTTP_CODE process_read();//解析HTTP请求

    //这块函数是HTTP调用用来分析HTTP请求
    HTTP_CODE parse_request_line(char* text);//解析请求首行
    HTTP_CODE parse_headers(char* text);//解析请求头
    HTTP_CODE parse_content(char* text);//解析消息体
    HTTP_CODE do_request();
    //获取一行数据
    char* get_line(){
        return m_read_buf +m_start_line;
    }
    LINE_STATUS parse_line();
private:
    //这块函数是用来生成响应报文的
    bool add_status_line( int status, const char* title );
    bool add_headers( int content_length );
    bool add_content_length( int content_length );
    bool add_content( const char* content );
    bool add_response(const char* format, ...);
    bool add_content_type();
    bool add_linger();
    bool add_blank_line();
    void unmap();

    void init();
public:
    void close_conn();
    void init(int fd, const sockaddr_in &address);
    bool read();//非阻塞的读
    bool write();//非阻塞的写
private:
    int m_sockfd;//HTTP连接的socket
    sockaddr_in m_address;//通信的socket地址

    char m_read_buf[READ_BUFFER_SIZE];//读缓冲区
    int m_read_idx;
    int m_checked_index;//正在分析的字符在读缓冲区的位置
    int m_start_line;//在解析行的起始位置
    CHECK_STATE m_check_state;//主状态机当前所处的状态

    char* m_url;
    char* m_version;
    METHOD m_method;
    char* m_host;
    bool m_linger;//http是否要保持连接
    int m_content_length;
    char m_real_file[FILENAME_LEN];

    struct stat m_file_stat;//目标文件的状态。通过它我们可以判断文件是否存在、是否可读等信息
    char* m_file_address;//客户端请求的文件被mmap到内存中的起始位置
    int m_write_idx;// 写缓冲区中待发送的字节数
    char m_write_buf[ WRITE_BUFFER_SIZE ];  // 写缓冲区
    int m_iv_count;//被写内存块的数量
    struct iovec m_iv[2];//采用writev来执行写操作，定义下面两个成员，其中m_iv_count表示被写内存块的数量
    int bytes_have_send;// 已经发送的字节
    int bytes_to_send;// 将要发送的字节

    std::string m_url_str;//m_url转换后的字符串类型
    
};
#endif
