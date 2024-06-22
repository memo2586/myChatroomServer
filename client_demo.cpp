#include <sys/socket.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <iostream>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <stdlib.h>

#include "message.h"

#define MAX_BUFFER_SIZE 1024
#define EVENT_LIMIT 65535

int connfd;
int epollfd;
int pipefd[2];
message_t send_msg;
message_t recv_msg;

int setnoblocking(int fd) {
    int old_ops = fcntl(fd, F_GETFL);
    int new_ops = old_ops | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_ops);
    return old_ops;
}

void addfd(int epollfd, int fd) {
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnoblocking(fd);
}

void addsig(int sig, void(*handler)(int)) {
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

void sig_int(int sig) {
    printf("close client.\n");
    char info[] = "left chatroom\n";
    send_msg.set_message(info);
    send(connfd, (char*)&send_msg, sizeof(send_msg), 0);
    close(connfd);
    close(pipefd[0]);
    close(pipefd[1]);
    close(epollfd);
    exit(0);
}

int main(int argc, char* argv[]) {
    if(argc < 4) {
        printf("waring: argc = %d", argc);
        return 1;
    }
    const char* ip = argv[1];
    const int port = atoi(argv[2]);
    const char* username = argv[3];
    const int room_num = atoi(argv[4]);
    
    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    inet_pton(AF_INET, ip, &address.sin_addr);

    connfd = socket(AF_INET, SOCK_STREAM, 0);
    assert(connfd >= 0);

    int ret = connect(connfd, (sockaddr*)&address, sizeof(address));
    // setnoblocking(connfd);
    send_msg.empty();
    send_msg.set_username(username);
    char info[] = "joined chatroom\n";
    send_msg.set_message(info);
    send_msg.set_room_number(room_num);
    ret = send(connfd, (char*)&send_msg, sizeof(send_msg), 0);
    printf("connection.\n");

    epoll_event events[EVENT_LIMIT];
    epollfd = epoll_create(5);
    assert(epollfd != -1);
    addfd(epollfd, connfd);
    addfd(epollfd, 0);

    ret = pipe(pipefd);
    assert(ret != -1);

    addsig(SIGINT, sig_int);

    char buf[MAX_BUFFER_SIZE];

    while(1) {
        int number = epoll_wait(epollfd, events, EVENT_LIMIT, -1);
        if((number < 0) && (errno != EAGAIN)) {
            printf("epoll failure.\n");
        }

        for(int i = 0; i < number; i++) {
            int sockfd = events[i].data.fd;
            int cur_event = events[i].events;
            /* recv event */
            if((sockfd == connfd) && (cur_event & EPOLLIN)) {
                recv_msg.empty();
                ret = recv(sockfd, (char*)&recv_msg, sizeof(recv_msg), 0);
                printf("%s: %s", recv_msg.get_username(), recv_msg.get_message());
            }
            /* studio event */
            else if((sockfd == 0) && (cur_event & EPOLLIN)) {
                ret = splice(0, NULL, pipefd[1], NULL, 32768, SPLICE_F_MORE | SPLICE_F_MOVE);
                assert(ret != -1);
                
                // 加工数据
                char buf[1024];
                memset(buf, '\0', sizeof(buf));
                send_msg.empty();
                read(pipefd[0], buf, sizeof(buf));
                send_msg.set_username(username);
                send_msg.set_message(buf);
                send_msg.set_room_number(room_num);

                ret = send(connfd, (char*)&send_msg, sizeof(send_msg), 0);
                assert(ret != -1);
            }
        }
    }

    close(connfd);
    return 0;
}
