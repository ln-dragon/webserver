#include <iostream>
#include "server.hpp"
#include <unistd.h>
#include <stdlib.h>

int main(int argc, char* argv[]){
    if(argc < 3){
        std::cout << "./a.out port path";
        return -1;
    }
    //转换为整形
    unsigned short port = atoi(argv[1]);
    //切换服务器的工作路径
    chdir(argv[2]);
    //初始化监听的套接字
    int lfd = initListenFd(port);
    //启动服务器程序
    epollRun(lfd);
    return 0;
}