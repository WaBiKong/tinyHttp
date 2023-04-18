#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <string.h>
#include <libgen.h>

#define BUFFER_SIZE 1023

int setnonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK; // 将文件描述符加上非阻塞属性
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

int unblock_connect(const char* ip, int port, int time) {

    int ret = 0;

    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &address.sin_addr);
    address.sin_port = htons(port);

    int sockfd = socket(PF_INET, SOCK_STREAM, 0);
    int fdopt = setnonblocking(sockfd);

    ret = connect(sockfd, (struct sockaddr*)&address, sizeof(address));
    if (ret == 0) {
        printf("connect with server immediately\n");
        fcntl(sockfd, F_SETFL, fdopt);
        return sockfd;
    } else if (errno != EINPROGRESS) { // 连接失败
        printf("unblock connect not support\n");
        return -1;
    }
    // ret == -1 && errno == EINPROGRESS，需要进一步判断是否连接成功
    fd_set writefds;
    struct timeval timeout;

    FD_SET(sockfd, &writefds);

    timeout.tv_sec = time;
    timeout.tv_usec = 0;

    ret = select(sockfd + 1, NULL, &writefds, NULL, &timeout);
    if (ret <= 0) {
        printf("connection time out\n");
        close(sockfd);
        return -1;
    }

    if (!FD_ISSET(sockfd, &writefds)) { // socket不可写，则说明连接失败
        printf("no events on sockfd found\n");
        close(sockfd);
        return -1;
    }

    // FD_ISSET(sockfd, &writefds) socket可写
    int error = 0;
    socklen_t length = sizeof(error);
    // 获取错误码
    if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error, &length) < 0) {
        printf("get socket option failed\n");
        close(sockfd);
        return -1;
    }
    // 错误码不为0表示连接失败
    if (error != 0) {
        printf("connection failed after select with the error: %d \n", error);
        close(sockfd);
        return -1;
    }
    // 错误码为0，表示connect成功，因为是非阻塞的，所以要在后面判断是否连接成功
    printf("connection ready after select with the socket: %d \n", sockfd);
    fcntl(sockfd, F_SETFL, fdopt);
    return sockfd;
}

int main(int argc, char* argv[]) {
    if (argc <= 2) {
        printf("usage: %s ip_address port_number\n", basename(argv[0]));
        return 1;
    }
    const char* ip = argv[1];
    int port = atoi(argv[2]);

    int sockfd = unblock_connect(ip, port, 10);
    if (sockfd < 0) {
        return 1;
    }
    shutdown(sockfd, SHUT_WR);
    sleep(200);
    printf("send data out\n");
    send(sockfd, "abc", 3, 0);
    //sleep( 600 );
    return 0;
}
