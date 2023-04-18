#define _GNU_SOURCE 1
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
#include <poll.h>
#include <libgen.h>

#define USER_LIMIT 5 // 最大用户数量
#define BUFFER_SIZE 64 // 读缓冲区大小
#define FD_LIMIT 65535 // 文件描述符限制

struct client_data {
    sockaddr_in address;    // 用户的socket地址
    char* write_buf;
    char buf[BUFFER_SIZE]; // 从用户那接收的数据
};

int setnonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
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
    inet_pton(AF_INET, ip, &address.sin_addr.s_addr);
    address.sin_port = htons(port);

    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);

    ret = bind(listenfd, (struct sockaddr*)&address, sizeof(address));
    assert(ret != -1);

    ret = listen(listenfd, 5);
    assert(ret != -1);

    // 定义用户数组，用来将socket属性和用户数据关联
    client_data* users = new client_data[FD_LIMIT];

    // 定义poll数组，并初始化
    pollfd fds[USER_LIMIT + 1];
    int user_counter = 0;
    for (int i = 1; i <= USER_LIMIT; ++i) {
        fds[i].fd = -1;
        fds[i].events = 0;
    }
    // 初始化listen
    fds[0].fd = listenfd;
    fds[0].events = POLLIN | POLLERR;
    fds[0].revents = 0;

    while (1) {
        ret = poll(fds, user_counter + 1, -1);
        if (ret < 0) {
            printf("poll failure\n");
            break;
        }
        
        // 接收客户数据，并把客户数据发送给每一个登录到该服务器上的客户端（数据发送者除外）
        for (int i = 0; i < user_counter + 1; ++i) {

            // 判断是否有新的连接
            if ((fds[i].fd == listenfd) && (fds[i].revents & POLLIN)) {

                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof(client_address);

                int connfd = accept(listenfd, (struct sockaddr*)&client_address, &client_addrlength);
                if (connfd < 0) {
                    printf("errno is: %d\n", errno);
                    continue;
                }
                
                // 连接人数太多则关闭新连接
                if (user_counter >= USER_LIMIT) {
                    const char* info = "too many users\n";
                    printf("%s", info);
                    send(connfd, info, strlen(info), 0);
                    close(connfd);
                    continue;
                }

                // 记录用户信息
                user_counter++;
                users[connfd].address = client_address;
                setnonblocking(connfd); // 将描述符设置为非阻塞

                // 将新连接加入到poll中
                fds[user_counter].fd = connfd;
                fds[user_counter].events = POLLIN | POLLRDHUP | POLLERR;
                fds[user_counter].revents = 0;
                printf("comes a new user, now have %d users\n", user_counter);

            } else if (fds[i].revents & POLLERR) { // 发生错误

                printf("get an error from %d\n", fds[i].fd);

                char errors[100];
                memset(errors, '\0', 100);
                socklen_t length = sizeof(errors);
                // 获取错误属性，即错误码
                if (getsockopt(fds[i].fd, SOL_SOCKET, SO_ERROR, &errors, &length) < 0) {
                    printf("get socket option failed\n");
                }
                continue;

            } else if (fds[i].revents & POLLRDHUP) { // 客户端断开连接，则服务器也断开

                users[fds[i].fd] = users[fds[user_counter].fd];
                close(fds[i].fd);
                fds[i] = fds[user_counter];
                i--;
                user_counter--;
                printf("a client left\n");

            } else if (fds[i].revents & POLLIN) { // 客户端发送信息过来

                int connfd = fds[i].fd;
                // 用users[connfd].buf是得每个用户发来的信息和自己的addr关联
                memset(users[connfd].buf, '\0', BUFFER_SIZE);
                ret = recv(connfd, users[connfd].buf, BUFFER_SIZE - 1, 0); // 接收信息
                printf("get %d bytes of client data %s from %d\n", ret, users[connfd].buf, connfd);
                
                if (ret < 0) { // 读取失败
                    if (errno != EAGAIN) { 
                        // 关闭和客户端的连接
                        close(connfd);
                        users[fds[i].fd] = users[fds[user_counter].fd];
                        fds[i] = fds[user_counter];
                        i--;
                        user_counter--;
                    }
                } else if (ret == 0) {
                    printf("code should not come to here\n");
                } else { // 通知其他客户端的socket连接准备接收数据
                    for (int j = 1; j <= user_counter; ++j) {
                        if (fds[j].fd == connfd) {
                            continue; // 不需要发给将这个信息发送来的客户端
                        }
                        // 将注册事件改为可写，即检测是否有要发送给客户端的数据
                        fds[j].events |= ~POLLIN;
                        fds[j].events |= POLLOUT; 
                        // 其余每个用户的写数据指针都指向这个用户的刚接收的数据
                        users[fds[j].fd].write_buf = users[connfd].buf;
                    }
                }
            } else if (fds[i].revents & POLLOUT) {
                int connfd = fds[i].fd;
                if (!users[connfd].write_buf) {
                    continue;
                }
                // 发送数据
                ret = send(connfd, users[connfd].write_buf, strlen(users[connfd].write_buf), 0);
                users[connfd].write_buf = NULL;
                fds[i].events |= ~POLLOUT;
                fds[i].events |= POLLIN; // 发送完后将注册事件改为可读，即是否有往这写数据的客户端
            }
        }
    }

    delete[] users;
    close(listenfd);
    return 0;
}
