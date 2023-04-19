#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <pthread.h>

#define MAX_EVENT_NUMBER 1024
#define TCP_BUFFER_SIZE 512 // TCP接收缓存大小
#define UDP_BUFFER_SIZE 1024 // UDP接收缓存大小

int setnonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

void addfd(int epollfd, int fd) {
    epoll_event event;
    event.data.fd = fd;
    //event.events = EPOLLIN | EPOLLET;
    event.events = EPOLLIN;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

int main(int argc, char* argv[]) {
    if (argc <= 2) {
        printf("usage: %s ip_address port_number\n", basename(argv[0]));
        return 1;
    }
    const char* ip = argv[1];
    int port = atoi(argv[2]);

    int ret = 0;

    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &address.sin_addr);
    address.sin_port = htons(port);

    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);

    ret = bind(listenfd, (struct sockaddr*)&address, sizeof(address));
    assert(ret != -1);

    ret = listen(listenfd, 5);
    assert(ret != -1);

    bzero(&address, sizeof(address)); // 将原来的二进制每一位设置为0
    address.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &address.sin_addr);
    address.sin_port = htons(port);
    int udpfd = socket(PF_INET, SOCK_DGRAM, 0); // 数据报传输形式的socket
    assert(udpfd >= 0);

    ret = bind(udpfd, (struct sockaddr*)&address, sizeof(address));
    assert(ret != -1);

    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(5);
    assert(epollfd != -1);
    addfd(epollfd, listenfd);
    addfd(epollfd, udpfd);

    while (1) {
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);

        if (number < 0) {
            printf("epoll failure\n");
            break;
        }

        for (int i = 0; i < number; i++) {

            int sockfd = events[i].data.fd;
            
            if (sockfd == listenfd) {  // 获取到tcp连接
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof(client_address);

                int connfd = accept(listenfd, (struct sockaddr*)&client_address, &client_addrlength);

                addfd(epollfd, connfd);
            } else if (sockfd == udpfd) { // 获取到udp连接
                char buf[UDP_BUFFER_SIZE];
                memset(buf, '\0', UDP_BUFFER_SIZE);

                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof(client_address);

                // udp面向无连接到，所以没有connect和accept，只有sendto和recvfrom
                ret = recvfrom(udpfd, buf, UDP_BUFFER_SIZE - 1, 0, (struct sockaddr*)&client_address, &client_addrlength);
                
                if (ret > 0) { // 将读取到的信息立马发回去
                    sendto(udpfd, buf, UDP_BUFFER_SIZE - 1, 0, (struct sockaddr*)&client_address, client_addrlength);
                }
            } else if (events[i].events & EPOLLIN) { // 在获取到tcp连接后，有获取到了socket通信数据
                char buf[TCP_BUFFER_SIZE];
                
                while (1) {
                    memset(buf, '\0', TCP_BUFFER_SIZE);

                    ret = recv(sockfd, buf, TCP_BUFFER_SIZE - 1, 0);

                    if (ret < 0) {
                        if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                            break;
                        }
                        close(sockfd);
                        break;
                    } else if (ret == 0) { // 客户端关闭连接
                        close(sockfd);
                    } else { // 将获取到的数据发回去
                        send(sockfd, buf, ret, 0);
                    }
                }
            } else {
                printf("something else happened \n");
            }
        }
    }

    close(listenfd);
    return 0;
}
