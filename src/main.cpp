#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>
#include <signal.h>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include "threadpool.h"
#include "http_conn.h"
#include <cassert>
#include <errno.h>
#define MAX_FD 65535 //
#define MAX_EVENT_NUMBER 10000

//设置捕捉信号处理
void addsig(int sig, void (sig_hander)(int)){
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));//将 sigaction 结构体清零
    sa.sa_flags = 0;
    sa.sa_handler = sig_hander;
    sigfillset(&sa.sa_mask);//在信号处理程序执行期间，所有其他信号都将被阻塞
    sigaction(sig, &sa, NULL);
}
//添加文件描述符到epoll中
extern void addfd(int epollfd, int fd, bool one_shot);
//删除文件描述符到epoll中
extern void removefd(int epollfd, int fd);
//修改文件描述符到epoll中
extern void modfd(int epollfd, int fd, int ev);

int main(int argc, char *argv[]){
    if(argc <= 2){
        printf("按照如下格式运行;%s ip_address port_number\n", basename(argv[0]));
        return -1;
    }
    //获取端口号
    char* ip = argv[1];
    int port = atoi(argv[2]);
    //对SIGPIE信号进行忽略
    addsig(SIGPIPE, SIG_IGN);
    //创建连接池,http_conn为传输过程中需要处理的类
    threadpool<http_conn>* pool = NULL;
    try{
        pool = new threadpool<http_conn>;
    }
    catch(...){
        return -1;
    }
    //为客户分配一个http_conn对象
    http_conn* users = new http_conn[MAX_FD];

    //网络连接代码
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    //设置端口复用
    int reuse = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    inet_pton(AF_INET, ip, &address.sin_addr);//将点分十进制地址转换为网络字节序格式
    int ret = 0;
    ret = bind(listenfd, (struct sockaddr*)&address, sizeof(address));
    assert(ret >= 0);
    ret = listen(listenfd, 8);
    assert(ret >= 0);
    //创建epoll
    struct epoll_event events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(5);
    //添加文件描述符到epoll中
    addfd(epollfd, listenfd, false);
    http_conn::m_epollfd = epollfd;
    //循环等待
    while(1){
        printf("epoll等待客户端传输的数据\n");
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if(number < 0  && (errno != EINTR)){
            printf("epoll fail\n");
            break;
        }
        for(int i=0; i<number; i++){
            int sockfd = events[i].data.fd;
            if(sockfd == listenfd){
                struct sockaddr_in clientaddr;
                socklen_t clientaddrlen = sizeof(clientaddr);
                int connfd = accept(sockfd, (struct sockaddr*)&clientaddr, &clientaddrlen);
                //连接失败
                if(connfd < 0){
                    printf("errno is: %d\n", errno);
                }
                //连接数量大于用户数量
                if(http_conn::m_user_count >= MAX_FD){
                    // show_error(connfd, "Internal server busy");
                    close(connfd);
                    continue;
                }
                //初始化连接客户
                users[connfd].init(connfd, clientaddr);
            }
            else if(events[i].events & (EPOLLRDHUP |EPOLLHUP | EPOLLERR)){
                users[sockfd].close_conn();
            }
            else if(events[i].events & EPOLLIN){
                if(users[sockfd].read()){
                    //一次性把数据都读完
                    pool->append(users + sockfd);
                }
                else{
                    users[sockfd].close_conn();
                }
            }
            else if(events[i].events & EPOLLOUT){
                if( !users[sockfd].write()){//一次性把数据写完
                    users[sockfd].close_conn();
                }
            }
        } 
    }
    close(epollfd);
    close(listenfd);
    delete [] users;
    delete pool;
    return 0;
}