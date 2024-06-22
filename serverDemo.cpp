#include <sys/types.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <iostream>
#include <assert.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <signal.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <set>
#include <map>
#include <unistd.h>
#include "threadpool.h" 
#include "message.h"

std::map<int, int> connfdToRoomID;  // 文件描述符和房间id的映射

class task {
public:
    task() {}
    task(int type, int fd, message_t msg) : _type(type), _connfd(fd), data(msg) {}

    void init(int type, int fd, message_t msg) {
        _type = type;
        _connfd = fd;
        data = msg;
    }

    int send_task(std::set<int>* connfdset) {
        for(auto i = connfdset->begin(); i != connfdset->end(); i++) {
            if(connfdToRoomID[*i] != connfdToRoomID[_connfd]) continue;
            if(*i == _connfd) continue;
            int ret = send(*i, (char*)&data, sizeof(data), 0);
            if(ret < 0) {
                printf("ERROR::THREADPOOL::CHILD::SEND_TASK\n");
                return ret;
            }
        }
        return 0;
    }

    int recv_task() {
        int ret = recv(_connfd, (char*)&data, sizeof(data), 0);
        if(ret < 0) {
            if(errno != EAGAIN){
                printf("ERROR::THREADPOOL::CHILD::TASK_RECV::RECV::%d.\n", errno);
                return -1;
            }
        }
        else if(ret == 0) {
            return 0;
        }

        connfdToRoomID[_connfd] = data.get_room_number();

        printf("User %s: %s", data.get_username(), data.get_message());
        return ret;
    }

public:
    /* 0.接收信息    1.发送信息 */
    int _type;
    int _connfd;    // 当 type = 0 时，指定向 _connfd 发送. 当 type = 1，指定无需向 _connfd 广播
    message_t data;
private:
};

int main(int argc, char* argv[]) {
    assert(argc == 3);
    char* ip = argv[1];
    int port = atoi(argv[2]);

    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_port = ntohs(port);
    inet_pton(AF_INET, ip, &address.sin_addr);

    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    assert(listen >= 0);
    int ret = bind(listenfd, (sockaddr*)&address, sizeof(address));
    assert(ret != -1);
    ret = listen(listenfd, 5);
    assert(ret != -1);

    threadpool<task>* instance = threadpool<task>::create(listenfd, 4);
    instance->run();
    return 0;
}
