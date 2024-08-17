#include <iostream>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/stat.h>
#include <assert.h>
#include <dirent.h>
#include <pthread.h>
#include <stdlib.h>
#include "server.hpp"

struct FdInfo{//由于pthread_create函数只能有一个参数传递，所以创建了一个结构体用来传递一个参数
    int fd;
    int epfd;
    pthread_t tid;
};

// 初始化监听的套接字
int initListenFd(unsigned short port){
    // 1. 创建监听的fd
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd == -1)
    {
        perror("socket");
        return -1;
    }
    // 2. 设置端口复用 
    int opt = 1;
    int ret = setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof opt);
    if (ret == -1)
    {
        perror("setsockopt");
        return -1;
    }
    // 3. 绑定
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    ret = bind(lfd, (struct sockaddr*)&addr, sizeof addr);
    if (ret == -1)
    {
        perror("bind");
        return -1;
    }
    // 4. 设置监听
    ret = listen(lfd, 128);
    if (ret == -1)
    {
        perror("listen");
        return -1;
    }
    // 返回fd
    return lfd;
}

// 和客户端建立连接
void* acceptClient(void* arg){
    std::cout<<"开始与客户端建立连接....";
    struct FdInfo* info = (struct FdInfo*)arg;
    int connfd = accept(info->fd, NULL,NULL);
    if(connfd == -1){
        perror("accept error");
        return nullptr;
    }
    //设置非阻塞
    int flag = fcntl(connfd, F_GETFL);
    flag |= O_NONBLOCK;
    fcntl(connfd, F_SETFL, flag);

    struct epoll_event ev;
    ev.data.fd = connfd;
    ev.events = EPOLLIN;
    int ret = epoll_ctl(info->epfd, EPOLL_CTL_ADD, connfd, &ev);
    if(ret == -1){
        perror("epoll_ctl");
        exit(1);
    }
    printf("acceptClient threadId: %ld\n", info->tid);
    free(info);
    return nullptr;
}

// 接收http请求
void* recvHttpRequest(void* arg){
    std::cout<<"开始接受数据...";
    struct FdInfo* info = (struct FdInfo*)arg;
    char buf[4096] = {0};
    char temp[1024] = {0};
    int len = 0, totle = 0;
    len = recv(info->fd, temp, sizeof(temp), 0);
    while(len > 0){
        if(totle + len < sizeof(buf)){
            memcpy(buf, temp,len);
        }
        totle += len;
    }
    if(len == -1 && errno == EAGAIN){//等于-1有两种情况，一种是读取完后，一种是读取错误
        //解析请求行
        char* pt = strstr(buf, "\r\n");//读取请求行
        int reqLen = pt - buf;
        buf[reqLen] = '\0';
        parseRequestLine(buf, info->fd);
    }
    else if(len == 0){
        //客户端断开连接
        epoll_ctl(info->epfd, EPOLL_CTL_DEL, info->fd, NULL);
        close(info->fd);
    }
    else{
        perror("recv");
    }
    printf("recvMsg threadId: %ld\n", info->tid);
    free(info);
    return nullptr;
}

// 启动epoll
int epollRun(int lfd){
    //创建epoll实例
    int epfd = epoll_create(1000);
    if(epfd == -1){
        perror("epoll_create");
        return -1;
    }
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = lfd;
    int ret = epoll_ctl(epfd, EPOLL_CTL_ADD, lfd, &ev);
    if(ret == -1){
        perror("epoll_ctl");
        return -1;
    }
    struct epoll_event envs[1204];
    int size = sizeof(envs)/sizeof(struct epoll_event);
    while(1){
        int nums = epoll_wait(epfd, envs, size,-1);
        for(int i=0; i<nums; i++){
            struct FdInfo* info = (struct FdInfo*)malloc(sizeof(struct FdInfo));//初始化结构体
            int fd = envs[i].data.fd;
            info->epfd = epfd;
            info->fd = fd;
            if(fd  == lfd){
                //和客户端建立连接
                // acceptClient(lfd,epfd);//单线程采用的方式
                pthread_create(&info->tid, NULL, acceptClient, info);
                std::cout<<"与客户端建立连接完成";
            }
            else{
                //主要是接受对端数据
                // recvHttpRequest(fd, epfd);
                pthread_create(&info->tid, NULL, recvHttpRequest, info);
                std::cout<<"与客户端接受数据完成";
            }
        }
    }
    return 0;
}

// 解析请求行
int parseRequestLine(const char* line, int cfd){
   char method[12];
    char path[1024];

    std::sscanf(line, "%s %s", method, path);
    std::string methodStr(method);
    std::string pathStr(path);

    std::cout << "method: " << methodStr << " " << "path: " << pathStr << std::endl;

    if (strcasecmp(methodStr.c_str(), "GET") != 0) {
        return -1;
    }
    //处理客户端请求的静态资源(目录或文件)
    // char* file = NULL;
    std::string file;
    if(path == "/"){
        file = "./";
    }
    else{
        // file = path + 1;
        file = pathStr.substr(1); // 跳过第一个字符 '/'
    }
    //获取文件属性
    struct stat st;
    int ret = stat(file.c_str(), &st);
    if(ret == -1){
        //文件不存在
        sendHeadMsg(cfd, 404,"Not Find", getFileType(".html"), -1);
        sendFile("404.html", cfd);
        return 0;
    }
    //判断文件类型
    if(S_ISDIR(st.st_mode)){
        //把这个目录中的内容发送给客户端
        sendHeadMsg(cfd, 200,"OK", getFileType(".html"), -1);
        sendDir(file.c_str(), cfd);
    }
    else{
        //把文件的内容发送给客户端
        sendHeadMsg(cfd, 200,"OK", getFileType(file.c_str()), st.st_size);
        sendFile(file.c_str(), cfd);
    }
    return 0;
}
// 发送文件
int sendFile(const char* fileName, int cfd){
     //1.打开文件
    int fd = open(fileName, O_RDONLY);
    assert(fd > 0);
    while(1){//边读边发送数据
        char buf[1024];
        int len = read(fd, buf, sizeof(buf));
        if(len > 0){
            send(cfd, buf, len, 0);//发送给客户端
            usleep(10);//这非常重要，保证发送端发送的不会太快
        }
        else if(len == 0){
            break;
        }
        else{
            perror("read");
        }
    }
    return 0;
}
// 发送响应头(状态行+响应头)
int sendHeadMsg(int cfd, int status, const char* descr, const char* type, int length){
    //状态行
    char buf[4096] = {0};
    sprintf(buf, "http/1.1 %d %s\r\n", status, descr);
    //响应头
    sprintf(buf+strlen(buf), "content-type:%s\r\n", type);
    sprintf(buf+strlen(buf), "content-length:%d\r\n", length);
    //发送数据给客户端
    send(cfd, buf, strlen(buf), 0);
    return 0;
}
const char* getFileType(const char* name)
{
    // a.jpg a.mp4 a.html
    // 自右向左查找‘.’字符, 如不存在返回NULL
    const char* dot = strrchr(name, '.');
    if (dot == NULL)
        return "text/plain; charset=utf-8";	// 纯文本
    if (strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0)
        return "text/html; charset=utf-8";
    if (strcmp(dot, ".jpg") == 0 || strcmp(dot, ".jpeg") == 0)
        return "image/jpeg";
    if (strcmp(dot, ".gif") == 0)
        return "image/gif";
    if (strcmp(dot, ".png") == 0)
        return "image/png";
    if (strcmp(dot, ".css") == 0)
        return "text/css";
    if (strcmp(dot, ".au") == 0)
        return "audio/basic";
    if (strcmp(dot, ".wav") == 0)
        return "audio/wav";
    if (strcmp(dot, ".avi") == 0)
        return "video/x-msvideo";
    if (strcmp(dot, ".mov") == 0 || strcmp(dot, ".qt") == 0)
        return "video/quicktime";
    if (strcmp(dot, ".mpeg") == 0 || strcmp(dot, ".mpe") == 0)
        return "video/mpeg";
    if (strcmp(dot, ".vrml") == 0 || strcmp(dot, ".wrl") == 0)
        return "model/vrml";
    if (strcmp(dot, ".midi") == 0 || strcmp(dot, ".mid") == 0)
        return "audio/midi";
    if (strcmp(dot, ".mp3") == 0)
        return "audio/mpeg";
    if (strcmp(dot, ".ogg") == 0)
        return "application/ogg";
    if (strcmp(dot, ".pac") == 0)
        return "application/x-ns-proxy-autoconfig";
    return "text/plain; charset=utf-8";
}
// 发送目录-从服务器中读取目录发送给客户端
int sendDir(const char* dirName, int cfd){
    char buf[4096] ={0};
    sprintf(buf, "<html><head><title>%s</title><head><body><table>",dirName);
    struct dirent** namelist;
    int num = scandir(dirName, &namelist, NULL, alphasort);
    for(int i=0; i<num; i++){
        //取出文件名namelist指向的是一个指针数组struct dirent* temp[]
        char* name = namelist[i]->d_name;
        struct stat st;
        char subPath[1024] = {0};
        sprintf(subPath, "%s/%s", dirName, name);
        stat(name, &st);
        if(S_ISDIR(st.st_mode)){
            //a标签 <a href="">name</a>
            sprintf(buf + strlen(buf), "<tr><td> <a href=\"%s/\">%s</a></td><td>%ld</td</tr>", name,name, st.st_size);//将名字添加到html文件中
        }
        else{
            sprintf(buf + strlen(buf), "<tr><td> <a href=\"%s\">%s</a></td><td>%ld</td</tr>", name,name, st.st_size);
        }
        send(cfd, buf, strlen(buf),0);
        // meset(buf, 0, sizeof(buf));
        free(namelist[i]);
    }
    sprintf(buf, "</table></body></html>");
    free(namelist);
    return 0;
}
int hexToDec(char c){
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return 0;
}
void decodeMsg(char* to, char* from){
    for (; *from != '\0'; ++to, ++from)
    {
        // isxdigit -> 判断字符是不是16进制格式, 取值在 0-f
        // Linux%E5%86%85%E6%A0%B8.jpg
        if (from[0] == '%' && isxdigit(from[1]) && isxdigit(from[2]))
        {
            // 将16进制的数 -> 十进制 将这个数值赋值给了字符 int -> char
            // B2 == 178
            // 将3个字符, 变成了一个字符, 这个字符就是原始数据
            *to = hexToDec(from[1]) * 16 + hexToDec(from[2]);

            // 跳过 from[1] 和 from[2] 因此在当前循环中已经处理过了
            from += 2;
        }
        else
        {
            // 字符拷贝, 赋值
            *to = *from;
        }

    }
    *to = '\0';
}