#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include "locker.h"
#include "threadpool.h"
#include "http_conn.h"
#include <signal.h>
#include <assert.h>

#define MAX_FD 65535            // 最大文件描述符个数
#define MAX_EVENT_NUMBER 100000 // 最大监听的个数

//添加文件描述符到epoll
extern void addfd(int epollfd, int fd, bool one_shot);
//从epoll中删除文件描述符
extern void removefd(int epollfd, int fd);
extern void modfd(int epollfd,int fd,int ev);

//添加信号捕捉
void addsig(int sig, void(handler)(int))
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

int main(int argc, char *argv[])
{
    if (argc <= 1)
    {
        printf("按照如下格式允许：%s port_number\n", basename(argv[0]));
        exit(-1);
    }

    //获取端口号
    int port = atoi(argv[1]);
    //回调忽略它
    addsig(SIGPIPE, SIG_IGN);

    //初始化threadpool
    threadpool<http_conn> *pool = NULL;
    try
    {
        pool = new threadpool<http_conn>;
    }
    catch (...)
    {
        exit(-1);
    }

    //创建一个数组用于保存所有的客户端信息
    http_conn *users = new http_conn[MAX_FD];

    int listenfd = socket(PF_INET, SOCK_STREAM, 0);

    //设置端口复用
    int reuse = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    //绑定
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    bind(listenfd, (struct sockaddr *)&address, sizeof(address));

    //监听
    listen(listenfd, 5);

    //创建epoll对象，事件数组，添加
    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(5);
    //向epoll添加监听fd。
    addfd(epollfd,listenfd,false);
    http_conn::m_epollfd = epollfd;
    
    while(true){
        //主线程持续监测是否有事件发生。
        int num = epoll_wait(epollfd,events,MAX_EVENT_NUMBER,-1);
        if(num < 0 && (errno != EINTR)){
            printf("epoll failure \n");
            break;
        }

        //循环遍历events数组
        for(int i =0;i<num;++i){
            int sockfd = events[i].data.fd;
            if(sockfd == listenfd){
                struct sockaddr_in client_address;
                socklen_t client_addrlen = sizeof(client_address);
                int connfd = accept(listenfd,(struct sockaddr*)&client_address,&client_addrlen);

                if(http_conn::m_user_count >= MAX_FD){
                    //最大支持的连接数满了
                    //给客户端写一个信息 ： 服务器正忙。
                    close(connfd);
                }
                //将新的客户数据初始化，放入数组中
                users[connfd].init(connfd,client_address);
            }else if(events[i].events&(EPOLLRDHUP |EPOLLHUP | EPOLLERR)){
                users[sockfd].close_conn();
            }else if(events[i].events & EPOLLIN){
                //检测到有读事件，一次性读出来
                if(users[sockfd].read()){
                    pool -> append(users + sockfd);
                }else{
                    users[sockfd].close_conn();
                }
            }else if(events[i].events & EPOLLOUT){
                if(!users[sockfd].write()){
                    users[sockfd].close_conn();
                }
            }
        }
    }
    close(epollfd);
    close(listenfd);
    delete[] users;
    delete pool;

    return 0;
}