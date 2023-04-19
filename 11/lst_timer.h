#ifndef LST_TIMER
#define LST_TIMER

#include <time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdio.h>

#define BUFFER_SIZE 64

class util_timer; // 前向声明

// 用户数据结构：客户端socket地址、socket文件描述符、读缓存和定时器
struct client_data {
    sockaddr_in address;
    int sockfd;
    char buf[BUFFER_SIZE];
    util_timer * timer;
};

// 定时器类，类似于双向链表的节点
class util_timer {
    public:
        util_timer(): prev(NULL), next(NULL) {}

    public:
        time_t expire; // 任务的超时时间，绝对时间
        void (*cb_func)(client_data *); // 任务回调函数
        client_data * user_data; // 回调函数处理的客户数据，由定时器的执行者传递给回调函数
        util_timer * prev; // 指向前一个定时器
        util_timer * next; // 指向下一个定时器
};

// 定时器链表，一个带有头节点和尾节点的双向升序链表
class sort_timer_lst {
    public:
        sort_timer_lst(): head(NULL), tail(NULL) {}
        ~sort_timer_lst() { // 析构函数
            util_timer * tmp = head;
            while (tmp) {
                head = tmp->next;
                delete tmp;
                tmp = head;
            }
        }
        // 添加定时器
        void add_timer(util_timer * timer) {
            if (!timer) {
                return;
            }
            if (!head) { // 头节点为空
                head = tail = timer;
                return;
            }
            if (timer->expire < head->expire) { // 新加的更小则让他当头节点
                timer->next = head;
                head->prev = timer;
                head = timer;
                return;
            }
            // 将timer插入到head之后合适位置
            add_timer(timer, head);
        }
        // 修改某个定时器并调整其在链表中的位置
        void adjust_timer(util_timer * timer) {
            if (!timer) {
                return;
            }
            util_timer * tmp = timer->next;
            // 如果修改的为尾节点或修改后仍小于下一个，则不需要位置
            if (!tmp || (timer->expire < tmp->expire)) {
                return;
            }
            if (timer == head) { // 如果修改的为头节点
                head = head->next;
                head->prev = NULL;
                timer->next = NULL;
                add_timer(timer, head);
            } else {
                timer->prev->next = timer->next;
                timer->next->prev = timer->prev;
                add_timer(timer, timer->next);
            }
        }
        // 删除指定定时器
        void del_timer(util_timer * timer) {
            if (!timer) {
                return;
            }
            if ((timer == head) && (timer == tail)) { // 如果只有一个节点
                delete timer;
                head = NULL;
                tail = NULL;
                return;
            }
            if (timer == head) { // 如果timer为头节点
                head = head->next;
                head->prev = NULL;
                delete timer;
                return;
            }
            if (timer == tail) { // 如果timer为尾节点
                tail = tail->prev;
                tail->next = NULL;
                delete timer;
                return;
            }

            // 如果timer为中间节点
            timer->prev->next = timer->next;
            timer->next->prev = timer->prev;
            delete timer;
        }
        // SIGALRM信号每触发一次就执行一次头节点所对应的定时器的任务，并将其移出
        // 头节点绝对时间最小，且是升序，所以执行头节点的就行
        void tick() {
            if (!head) {
                return;
            }
            printf("timer tick\n");
            time_t cur = time(NULL); // 记录当前系统时间
            util_timer * tmp = head;
            while (tmp) { // 可能执行一个回调函数时下一个定时器的时间又到了，所以要while并判断
                if (cur < tmp->expire) { // 对比定时器时间和系统时间判断是否到期
                    break;
                }
                // cb_func: 回调函数
                // user_date: 回调函数处理的客户数据，由定时器的执行者传递给回调函数
                tmp->cb_func(tmp->user_data); // 信号处理
                
                head = tmp->next;
                if (head) {
                    head->prev = NULL;
                }
                delete tmp;
                tmp = head;
            }
        }

    private:
        void add_timer(util_timer * timer, util_timer * lst_head) {
            util_timer * prev = lst_head;
            util_timer * tmp = prev->next;
            while (tmp) {
                if (timer->expire < tmp->expire) {
                    prev->next = timer;
                    timer->next = tmp;
                    tmp->prev = timer;
                    timer->prev = prev;
                    break;
                }
                prev = tmp;
                tmp = tmp->next;
            }
            if (!tmp) {
                prev->next = timer;
                timer->prev = prev;
                timer->next = NULL;
                tail = timer;
            }

        }

    private:
        util_timer * head;
        util_timer * tail;
};

#endif
