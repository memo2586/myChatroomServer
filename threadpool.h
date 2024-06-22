#ifndef THREADPOOL_H
#define THREADPOOL_H

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
#include <vector>
#include <queue>
#include <set>
#include <unistd.h>

#include "message.h"

#define DEFALUT_THREAD_NUMBER 2


/*  模板类 T 中要实现
    task(int, int, message_t);
    int task(int, int, message_t);
    int send_task(std::set<int>*);
    int recv_task();
*/
template <typename T>
class threadpool {
public:
    static threadpool<T>* create(int listenfd, int thread_num = DEFALUT_THREAD_NUMBER);
    ~threadpool() {}
    void run();

private:
    threadpool(int listenfd, int thread_num):
    m_start(false), m_thread_num(thread_num), m_listenfd(listenfd) {
        assert((thread_num > 0) && (thread_num < MAX_THREAD_NUMBER));
        pthread_mutex_init(&m_mutex, NULL);
        pthread_cond_init(&m_cond, NULL);
    }
    /* 单例：取消拷贝和赋值 */
    threadpool(const threadpool<T> &) = delete;
    void operator = (const threadpool<T> &) = delete;

private:
    static void* run_parent_thread(void* args);
    static void* run_child_thread(void* args);
    void lock_queue() { pthread_mutex_lock(&m_mutex); }
    void unlock_queue() { pthread_mutex_unlock(&m_mutex); }
    bool have_task() { return !m_taskq.empty(); }
    void waitfor_task() { pthread_cond_wait(&m_cond, &m_mutex); }
    void signal_thread() { pthread_cond_signal(&m_cond); };
    T pop() {
        T temp = m_taskq.front();
        m_taskq.pop();
        return temp;
    }
    void push(const T& item) {
        lock_queue();
        m_taskq.push(item);
        signal_thread();
        unlock_queue();
    }

private:
    static const int MAX_THREAD_NUMBER = 16;
    static const int MAX_USER_NUMBER = 65535;
    static const int MAX_EVENT_NUMBER = 1024;
    bool m_start;               
    int m_thread_num;           // 线程数
    int m_listenfd;             // 监听 socket 的文件描述符
    int m_epollfd;              // I/O 复用
    std::set<int> m_connfds;    // 已经打开的连接socket的集合
    std::queue<T> m_taskq;      // 任务队列
    pthread_mutex_t m_mutex;    // 互斥锁
    pthread_cond_t m_cond;      // 条件变量
    pthread_t* m_threads;       // 所有线程的信息
    static threadpool<T>* m_instance;   // 单例模式类指针
};

/* 单例：类外初始化 */
template<typename T>
threadpool<T>* threadpool<T>::m_instance = NULL;

/* 用于处理信号的管道，以实现统一事件源 */
static int sig_pipefd[2];

/* 设置文件描述符 fd 为非阻塞 */
static int setnoblocking(int fd) {
    int old_opt = fcntl(fd, F_GETFL);
    int new_opt = old_opt | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_opt);
    return old_opt;
}

/* 将 fd 添加到 epollfd 标识的内核事件表 */
static void addfd(int epollfd, int fd) {
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnoblocking(fd);
}

/* 将 epollfd 标识的内核事件表中 fd 上的所有注册事件删除，同时关闭 fd */
static void removefd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

/* 将信号发送到主循环（统一事件源） */
static void sig_handler(int sig) {
    int save_errno = errno;
    int msg = sig;
    send(sig_pipefd[1], (char*)&msg, 1, 0);
    errno = save_errno;
}

/* 添加信号并设置信号处理函数 */
static void addsig(int sig, void(handler)(int), bool restart = true) {
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if(restart) {
        sa.sa_flags |= SA_RESTART;
    }
    sigfillset(&sa.sa_mask);    // 处理函数执行时屏蔽所有信号
    assert(sigaction(sig, &sa, NULL) != -1);
}

/* 单例：获取单例 */
template<typename T>
threadpool<T>* threadpool<T>::create(int listenfd, int thread_num) {
    static pthread_mutex_t mutex;
    pthread_mutex_init(&mutex, NULL);
    if(m_instance == NULL) {
        pthread_mutex_lock(&mutex);
        // 避免等待锁的线程再次实例化
        if(m_instance == NULL) {
            m_instance = new threadpool<T> (listenfd, thread_num);
        }
    }
    pthread_mutex_unlock(&mutex);
    pthread_mutex_destroy(&mutex);
    return m_instance;
}

template<typename T>
void* threadpool<T>::run_parent_thread(void* args) {
    threadpool<T>* instance = static_cast<threadpool<T> *>(args);
    printf("parent start with %d.\n", (unsigned long)pthread_self());

    int ret = 0;
    epoll_event events[MAX_EVENT_NUMBER];
    // T* users = new T[MAX_USER_NUMBER];
    // assert(users);

    int listenfd = instance->m_listenfd;
    instance->m_epollfd = epoll_create(5);
    int epollfd = instance->m_epollfd;
    assert(epollfd != -1);
    ret = socketpair(AF_UNIX, SOCK_STREAM, 0, sig_pipefd);
    int pipefd = sig_pipefd[0];
    assert(ret != -1);

    setnoblocking(pipefd);
    addfd(epollfd, listenfd);
    addfd(epollfd, pipefd);
    addsig(SIGTERM, sig_handler);
    addsig(SIGINT, sig_handler);
    addsig(SIGPIPE, SIG_IGN);

    int number = 0;
    while(instance->m_start) {
        number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if((number < 0) && (errno != EINTR)) {
            printf("parent: epoll failuer.\n");
            instance->m_start = false;
            break;
        }

        for(int i = 0; i < number; i++) {
            int cur_fd = events[i].data.fd;
            int cur_event = events[i].events;
            // 事件：新连接
            if((cur_fd == listenfd) && (cur_event & EPOLLIN)) {
                sockaddr_in user_addr;
                socklen_t addrlen = sizeof(user_addr);
                int connfd = accept(listenfd, (sockaddr*)&user_addr, &addrlen);
                if(connfd < 0) {
                    printf("PARENT::SOCKET::ACCEPT::FAILED.\n");
                    continue;
                }
                addfd(epollfd, connfd);
                instance->m_connfds.insert(connfd);
                sockaddr_in conn_addr;
                socklen_t conn_addrlen = sizeof(conn_addr);
                getpeername(connfd, (sockaddr*)&conn_addr, &conn_addrlen);
                char ip[12];
                int port = ntohs(conn_addr.sin_port);;
                inet_ntop(AF_INET, &conn_addr.sin_addr, ip, INET_ADDRSTRLEN);
                printf("accept: %s:%d.\n", ip, port);
            }
            // 事件：信号
            else if((cur_fd == pipefd) && (cur_event & EPOLLIN)) {
                int sig;
                char signals[1024];
                ret = recv(pipefd, signals, 1024, 0);
                if(ret <= 0) {
                    continue;
                }
                else {
                    for(int idx = 0; idx < ret; idx++) {
                        switch (signals[idx])
                        {
                        case SIGINT:
                            instance->m_start = false;
                            // 唤醒等待锁的阻塞线程
                            pthread_cond_broadcast(&instance->m_cond);
                            break;
                        
                        default:
                            break;
                        }
                    }
                }
            }
            else if(cur_event & EPOLLIN) {
                T new_task(0, cur_fd, message_t());
                instance->push(new_task);
            }
        }
    }

    // delete[] users;
    printf("praent exit.\n");
    pthread_exit(NULL);
}

template<typename T>
void* threadpool<T>::run_child_thread(void* args) {
    threadpool<T>* instance = static_cast<threadpool<T> *>(args);
    printf("child start with %d.\n", (unsigned long)pthread_self());

    int ret = 0;
    while(instance->m_start) {

        /* 等待任务 */
        instance->lock_queue();
        while(!instance->have_task() && instance->m_start) {
            instance->waitfor_task();
        }

        // 如果被从阻塞中唤醒但线程池已停止运行，退出循环
        if(!instance->m_start) {
            instance->unlock_queue();
            break;
        }


        T task = instance->pop();
        instance->unlock_queue();

        /* 任务处理 */
        if(task._type == 0) {
            ret = task.recv_task();
            // 错误或连接关闭
            if(ret < 0) {
                instance->m_connfds.erase(task._connfd);
                removefd(instance->m_epollfd, task._connfd);
            }
            else if(ret == 0) {
                printf("client%d close.\n", task._connfd);
                instance->m_connfds.erase(task._connfd);
                removefd(instance->m_epollfd, task._connfd);
            }
            /* 将新消息的广播任务添加到任务队列 */
            else {
                T new_task(1, task._connfd, task.data);
                instance->push(new_task);
            }
        }
        else if(task._type == 1) {
            ret = task.send_task(&instance->m_connfds);
        }
    }

    printf("child exit.\n");
    pthread_exit(NULL);
}

template<typename T>
void threadpool<T>::run() {
    assert(m_start == false);
    printf("create %d thread.\n", m_thread_num);
    m_start = true;

    m_threads = new pthread_t[m_thread_num];
    pthread_create(&m_threads[0], NULL, run_parent_thread, this);
    for(int i = 1; i < m_thread_num; i++) {
        pthread_create(&m_threads[i], NULL, run_child_thread, this);
    }

    for(int i = 0; i < m_thread_num; i++) {
        pthread_join(m_threads[i], NULL);
    }

    delete[] m_threads;
    pthread_mutex_destroy(&m_mutex);
    pthread_cond_destroy(&m_cond);
    printf("threadpool exit.\n");
}

#endif
