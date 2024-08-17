#include "http_conn.h"

// 定义HTTP响应的一些状态信息
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";

// 网站的根目录
const char* doc_root = "/home/dragon/mywebserver/resources";
int http_conn::m_epollfd = -1;
int http_conn::m_user_count = 0;

http_conn::http_conn(){
    //构造函数
}
http_conn::~http_conn(){
    //析构函数
}
//设置非阻塞
int  setnonblocking(int fd){
    int old_flag = fcntl(fd, F_GETFL);
    int new_flag = old_flag | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_flag);
    return old_flag;
}
//添加文件描述符到epoll中
void addfd(int epollfd, int fd, bool one_shot){
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLRDHUP;
    if(one_shot){
        event.events | EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    //设置文件描述符非阻塞
    setnonblocking(fd);
}
//删除文件描述符到epoll中
void removefd(int epollfd, int fd){
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}
//修改文件描述符
void modfd(int epollfd, int fd, int ev){
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}
void http_conn::init(){
    m_check_state = CHECK_STATE_REQUESTLINE;//初始化状态为解析请求首行
    m_checked_index = 0;
    m_start_line = 0;
    m_read_idx = 0;
    m_write_idx = 0;

    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_host = 0;
    m_linger = false;
    m_content_length = 0;

    bytes_have_send = 0;
    bytes_to_send = 0;
    bzero(m_read_buf, READ_BUFFER_SIZE);
    bzero(m_write_buf, READ_BUFFER_SIZE);
    bzero(m_real_file, FILENAME_LEN);
}
//初始化连接
void http_conn::init(int sockfd, const sockaddr_in &addr){
    m_sockfd = sockfd;
    m_address = addr;
    //设置端口复用
    int reuse = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    addfd(m_epollfd, sockfd, true);
    m_user_count++;

    init();
}
//关闭连接
void http_conn::close_conn(){
    if(m_sockfd != -1 ){
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}
//非阻塞的读，循环读取客户数据，直到无数据可读或者对方关闭连接
bool http_conn::read(){
    if(m_read_idx >= READ_BUFFER_SIZE){
        return false;
    }
    int bytes_read = 0;//读取到的字节数
    //循环读取数据
    while(1){
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        if(bytes_read == -1){
            if(errno == EAGAIN || errno == EWOULDBLOCK){
                break;//没有数据
            }
            return false;
        }
        else if(bytes_read == 0){
            return false;//对方关闭连接
        }
        m_read_idx += bytes_read;
    }
    printf("从套接字中读取数据到buffer中\n");
    return true;
}
//非阻塞的写
bool http_conn::write(){
    int temp = 0;
    
    if(bytes_to_send == 0){
        // 将要发送的字节为0，这一次响应结束
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        init();
        return true;
    }
    
    while(1){
        printf("开始写数据到套接字中\n");
        //分散写
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if(temp <= -1){
            // 如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件，虽然在此期间，
            // 服务器无法立即接收到同一客户的下一个请求，但可以保证连接的完整性。
            if(errno == EAGAIN){
                modfd(m_epollfd, m_sockfd, EPOLLOUT);
                return true;
            }
            unmap();
            return false;
        }

        bytes_have_send += temp;
        bytes_to_send -= temp;

        if(bytes_have_send >= m_iv[0].iov_len){
            //头已经发送完毕
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }
        else{
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - temp;
        }
        if (bytes_to_send <= 0)
        {
            // 没有数据要发送了
            unmap();
            modfd(m_epollfd, m_sockfd, EPOLLIN);
            printf("写完数据了,修改文件描述符\n");
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
//线程池调用工作队列中的数据处理HTTP请求的入口函数
void http_conn::process(){
    //解析请求响应
    HTTP_CODE read_ret = process_read();
    if(read_ret == NO_REQUEST){
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return ;
    }
    printf("解析完HTTP的请求消息\n");
    //生成响应
    bool write_ret = process_write(read_ret);
    if( !write_ret){
        close_conn();
    }
    modfd(m_epollfd, m_sockfd, EPOLLOUT);
    printf("生成完响应的消息\n");
}
//解析HTTP请求
http_conn::HTTP_CODE http_conn::process_read(){
    LINE_STATUS line_status  = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char* text = 0;

    printf("开始解析HTTP数据\n");
    while(((m_check_state == CHECK_STATE_CONTENT) && (line_status == LINE_OK))
                || ((line_status = parse_line()) == LINE_OK)){
        //解析到一行完整的数据或者解析到请求体
        text = get_line();//获取一行数据
        m_start_line = m_checked_index;
        printf("got 1 http line:%s\n", text);

        switch (m_check_state)
        {
            case CHECK_STATE_REQUESTLINE:{
                ret = parse_request_line(text);
                if(ret == BAD_REQUEST){
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADER:{
                ret = parse_headers(text);
                if(ret == BAD_REQUEST){
                    return BAD_REQUEST;
                }
                else if(ret == GET_REQUEST){
                    return do_request();
                }
                break;
            }
            case CHECK_STATE_CONTENT:{
                ret = parse_content(text);
                if(ret == GET_REQUEST){
                    return do_request();
                }
                line_status = LINE_OPEN;
                break;
            }
            default:{
                return INTERNAL_ERROR;
            }
        }
    }
    return NO_REQUEST;
}
//解析请求首行，获取请求方法和url等
http_conn::HTTP_CODE http_conn::parse_request_line(char* text){
    // //GET /index.html HTTP/1.1
    // printf("解析请求首行\n");
    // m_url = strpbrk(text, "\t");//在txt中查找'\t'的指针位置
    // if (! m_url) { 
    //     return BAD_REQUEST;
    // }
    // *m_url++  = '\0';
    // //GET\0/index.html HTTP/1.1
    // char* method = text;//method指向text字符的起始地址
    // if(strcasecmp(method, "GET") == 0){
    //     m_method = GET;
    // }
    // else{
    //     return BAD_REQUEST;
    // }
    // // /index.html HTTP/1.1
    // m_version = strpbrk(m_url, "\t");
    // if( !m_version){
    //     return BAD_REQUEST;
    // }
    // *m_version++ = '\0';
    // // m_version += strspn(m_version, "\t");
    // if(strcasecmp(m_version, "HTTP/1.1") != 0){
    //     return BAD_REQUEST;
    // }
    // // http://192.168.1.1:10000/index.html
    // if(strncasecmp(m_url, "http://", 7) == 0){//函数用于比较 m_url 字符串的前 7 个字符和 "http://"是否相符合
    //     m_url += 7;
    //     m_url = strchr(m_url, '/');//使用 strchr 函数查找 m_url 中第一个 '/' 字符，并将 m_url 指针指向该位置
    // }
    // if(!m_url || m_url[0] != '/'){
    //     return BAD_REQUEST;
    // }
    // m_check_state = CHECK_STATE_HEADER;
    // printf("解析请求首行成功!\n");
    // return NO_REQUEST;
    printf("解析请求首行\n");
    
    // 在text中查找空格的指针位置
    m_url = strpbrk(text, " ");
    if (!m_url) {
        return BAD_REQUEST;
    }
    *m_url++ = '\0';

    char* method = text;
    if (strcasecmp(method, "GET") == 0) {
        m_method = GET;
    } else {
        return BAD_REQUEST;
    }

    m_url += strspn(m_url, " ");
    m_version = strpbrk(m_url, " ");
    if (!m_version) {
        return BAD_REQUEST;
    }
    *m_version++ = '\0';

    if (strcasecmp(m_version, "HTTP/1.1") != 0) {
        return BAD_REQUEST;
    }

    // 检查URL是否合法
    if (strncasecmp(m_url, "http://", 7) == 0) {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }

    if (!m_url || m_url[0] != '/') {
        return BAD_REQUEST;
    }

    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}
//解析请求头
http_conn::HTTP_CODE http_conn::parse_headers(char* text){
    //如果遇到空行，表示头部字段解析完毕
    printf("解析请求头\n");
    if(text[0] == '\0'){
        if(m_content_length != 0){
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }
    //处理Connection:字段
    else if(strncasecmp(text, "Connection:", 11) == 0){
        text += 11;
        text += strspn(text, "\t");
        if(strcasecmp(text, "keep-alive") == 0){
            m_linger = true;
        }
    }
    //处理Content-Length:字段
    else if(strncasecmp(text, "Content-Length:", 15) == 0){
        text += 15;
        text += strspn(text, "\t");
        m_content_length = atol(text);
    }
    //处理Host:字段
    else if(strncasecmp(text, "Host:", 5) == 0){
        text += 5;
        text += strspn(text, "\t");
        m_host = text;
    }
    else{
       printf( "oop! unknow header %s\n", text );
    }
    printf("解析请求头成功!\n");
    return NO_REQUEST;
}
//解析消息体
http_conn::HTTP_CODE http_conn::parse_content(char* text){
    printf("开始解析消息体!\n");
    if(m_read_idx >= (m_content_length + m_checked_index)){
        text[m_content_length] = '\0';
        return GET_REQUEST;
    }
    printf("解析消息体成功!\n");
    return NO_REQUEST;
}
// 当得到一个完整、正确的HTTP请求时，我们就分析目标文件的属性，
// 如果目标文件存在、对所有用户可读，且不是目录，则使用mmap将其
// 映射到内存地址m_file_address处，并告诉调用者获取文件成功
http_conn::HTTP_CODE http_conn::do_request(){
    // "/home/dragon/mywebserver/resources"
    printf("内存映射开始!\n");
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    strncpy(m_real_file + len, m_url, FILENAME_LEN - len -1);
    // 获取m_real_file文件的相关的状态信息，-1失败，0成功
    if(stat(m_real_file, &m_file_stat) < 0){
        return NO_RESOURCE;
    }
    // 判断访问权限
    if(!(m_file_stat.st_mode & S_IROTH)){
        return FORBIDDEN_REQUEST;
    }
    // 判断是否是目录
    // if(S_ISDIR(m_file_stat.st_mode)){
    //     return BAD_REQUEST;
    // }
    //以只读方式打开文件
    int fd = open(m_real_file, O_RDONLY);
    // 创建内存映射
    m_file_address = (char*)mmap(0,m_file_stat.st_mode, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    printf("创建内存映射成功!\n");
    return FILE_REQUEST;
}
//解析一行数据，判断依据\r\n
http_conn::LINE_STATUS http_conn::parse_line(){
    char temp;
    for( ; m_checked_index < m_read_idx; m_checked_index++){
        temp = m_read_buf[m_checked_index];
        if(temp == '\r'){
            if( (m_checked_index + 1) == m_read_idx){
                return LINE_OPEN;//不完整的
            }
            else if(m_read_buf[m_checked_index + 1] == '\n'){
                m_read_buf[m_checked_index++] = '\0';//先被赋值然后++
                m_read_buf[m_checked_index++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if(temp == '\n'){
            if((m_checked_index > 1) && (m_read_buf[m_checked_index-1] == '\r') ){
                m_read_buf[m_checked_index-1] = '\0';//先被赋值然后++
                m_read_buf[m_checked_index++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}


// 对内存映射区执行munmap操作,释放由mmap创建的这段内存空间
void http_conn::unmap(){
    if(m_file_address){
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}
// 根据服务器处理HTTP请求的结果，决定返回给客户端的内容
bool http_conn::process_write(HTTP_CODE ret){
    switch(ret){
        case INTERNAL_ERROR:{//服务器内部错误
            //状态行
            add_status_line(500, error_500_title);
            //响应头
            add_headers(strlen(error_500_form));
            //响应体
            if(!add_content(error_500_form)){
                return false;
            }
            break;
        }
        case BAD_REQUEST:{//客户端请求语法错误
            add_status_line(400, error_400_title);
            add_headers(strlen(error_400_form));
            if ( ! add_content( error_400_form ) ) {
                return false;
            }
            break;
        }   
        case NO_REQUEST:{
            add_status_line( 404, error_404_title );
            add_headers( strlen( error_404_form ) );
            if ( ! add_content( error_404_form ) ) {
                return false;
            }
            break;
        }
        case FORBIDDEN_REQUEST:{
            add_status_line( 403, error_403_title );
            add_headers(strlen( error_403_form));
            if ( ! add_content( error_403_form ) ) {
                return false;
            }
            break;
        }
        case FILE_REQUEST:{
            add_status_line(200, ok_200_title);
            add_headers(m_file_stat.st_size);
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_idx;
            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv_count = 2;

            bytes_to_send = m_write_idx + m_file_stat.st_size;
            return true;
        }
        default:
            return false;
    }
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}
//添加状态行
bool http_conn::add_status_line(int status, const char* title){
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}
//添加响应头部
bool http_conn::add_headers(int content_len){
    add_content_length(content_len);
    add_content_type();
    add_linger();
    add_blank_line();
}
//添加响应正文
bool http_conn::add_content(const char* content){
    return add_response("%s", content);
}
// 往写缓冲中写入待发送的数据
bool http_conn::add_response(const char* format, ...){
    if(m_write_idx >= WRITE_BUFFER_SIZE){
        return false;
    }
    va_list arg_list;//声明了一个用于存储可变参数列表的变量
    va_start(arg_list, format);//初始化 arg_list，使其指向可变参数列表中的第一个参数
    int len = vsnprintf(m_write_buf+m_write_idx, WRITE_BUFFER_SIZE-1-m_write_idx, format, arg_list);
    if(len >= (WRITE_BUFFER_SIZE -1 - m_write_idx)){
        return false;
    }
    m_write_idx += len;
    va_end( arg_list );
    return true;
}
//响应头部中的字段
bool http_conn::add_content_length(int  content_len){
    return add_response("Content-Length:%d\r\n", content_len);
}
bool http_conn::add_content_type(){
    return add_response("Content-Type:%s\r\n", "text/html");
    // return add_response("Content-Type:%s\r\n", "image/jpeg");
}
bool http_conn::add_linger(){
    return add_response("Connection:%s\r\n", (m_linger == true) ? "keep-alive":"close");
}
bool http_conn::add_blank_line(){
    return add_response( "%s", "\r\n" );
}


