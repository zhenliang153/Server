#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <arpa/inet.h>

#define MAX_EVENTS	1024
#define BUFLEN		4096
#define SERV_PORT	8080

void recvData(int fd, int events, void *arg);
void sendData(int fd, int events, void *arg);

struct myEvent_s
{
	int fd;			//要监听的文件描述符
	int events;		//对应的监听事件
	void *arg;		//指向自己结构体指针
	void (*call_back)(int fd, int events, void *arg);
	int status;		//是否在监听：1 在红黑数上（监听） 0 不在（不监听）
	char buf[BUFLEN];
	int len;
	long last_active;	//记录每次加入红黑数 g_efd的值
};

int g_efd;			//全局变量，保存epoll_create返回的文件描述符
struct myEvent_s g_events[MAX_EVENTS + 1];	//+1 最后一个用于listen fd

//将结构体myEvent_s成员变量初始化
void eventSet(struct myEvent_s *ev, int fd, void (*call_back)(int, int, void *), void *arg)
{
	ev->fd = fd;
	ev->call_back = call_back;
	ev->events = 0;
	ev->arg = arg;
	ev->status = 0;
	memset(ev->buf, 0, sizeof(ev->buf));
	ev->len = 0;
	ev->last_active = time(NULL);
	return;
}

//向epoll监听的红黑树添加一个文件描述符
void eventAdd(int efd, int events, struct myEvent_s *ev)
{
	int op = 0;
	struct epoll_event epv = {0, {0}};
	epv.data.ptr = ev;
	epv.events = ev->events = events;

	if(ev->status == 1)
	{
		op = EPOLL_CTL_MOD;
	}
	else
	{
		op = EPOLL_CTL_ADD;
		ev->status = 1;
	}

	if(epoll_ctl(efd, op, ev->fd, &epv) < 0)
	{
		printf("event add failed [fd=%d], events[%d]\n", ev->fd, events);
	}
	else
	{
		printf("event add OK [fd=%d], op=%d, events[%0X]\n", ev->fd, op, events);
	}

	return;
}

//从epoll监听的红黑树中删除一个文件描述符
void eventDel(int efd, struct myEvent_s *ev)
{
	struct epoll_event epv = {0, {0}};
	if(ev->status != 1)
		return;
	epv.data.ptr = NULL;
	ev->status = 0;
	epoll_ctl(efd, EPOLL_CTL_DEL, ev->fd, &epv);
}

void acceptConn(int lfd, int events, void *arg)
{
	struct sockaddr_in cin;
	int cfd, i;

	socklen_t len = sizeof(cin);
	if((cfd = accept(lfd, (struct sockaddr *)&cin, &len)) == -1)
	{
		printf("%s: accept, %s\n", __func__, strerror(errno));
		return;
	}

	do
	{
		for (i = 0; i < MAX_EVENTS; i++)
			if(g_events[i].status == 0)
				break;

		if(i == MAX_EVENTS)
		{
			printf("%s: max connect limit[%d]\n", __func__, MAX_EVENTS);
			break;
		}

		int flag = 0;
		if ((flag = fcntl(cfd, F_SETFL, O_NONBLOCK)) < 0)
		{
			printf("%s: fcntl nonblocking failed, %s\n", __func__, strerror(errno));
			break;
		}

		//给cfd设置一个myEvent_s结构体，回调函数设置为recvData
		eventSet(&g_events[i], cfd, recvData, &g_events[i]);
		//将cfd添加到红黑树g_efd中，监听读事件
		eventAdd(g_efd, EPOLLIN, &g_events[i]);
	}
	while(0);

	printf("new connect [%s:%d][time:%ld], pos[%d]\n",
			inet_ntoa(cin.sin_addr), ntohs(cin.sin_port), g_events[i].last_active, i);

	return;
}

void recvData(int fd, int events, void *arg)
{
	struct myEvent_s *ev = (struct myEvent_s *)arg;
	int len = recv(fd, ev->buf, sizeof(ev->buf), 0);

	eventDel(g_efd, ev);

	if(len > 0)
	{
		ev->len = len;
		ev->buf[len] = '\0';
		printf("C[%d]:%s\n", fd, ev->buf);

		eventSet(ev, fd, sendData, ev);

		eventAdd(g_efd, EPOLLOUT, ev);
	}
	else if(len == 0)
	{
		close(ev->fd);
		printf("[fd=%d] pos[%ld], closed\n", fd, ev-g_events);
	}
	else
	{
		close(ev->fd);
		printf("recv[fd=%d] error[%d]:%s\n", fd, errno, strerror(errno));
	}

	return;
}

void sendData(int fd, int events, void *arg)
{
	struct myEvent_s *ev = (struct myEvent_s *)arg;
	int len = send(fd, ev->buf, ev->len, 0);

	eventDel(g_efd, ev);

	if(len > 0)
	{
		printf("send[fd=%d], [%d]%s\n", fd, len, ev->buf);
		eventSet(ev, fd, recvData, ev);
		eventAdd(g_efd, EPOLLIN, ev);
	}
	else
	{
		close(ev->fd);
		printf("send[fd=%d] error %s\n", fd, strerror(errno));
	}

	return;
}

void initListenSocket(int efd, short port)
{
	struct sockaddr_in sin;

	int lfd = socket(AF_INET, SOCK_STREAM, 0);
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = INADDR_ANY;
	sin.sin_port = htons(port);

	bind(lfd, (struct sockaddr *)&sin, sizeof(sin));

	listen(lfd, 20);

	eventSet(&g_events[MAX_EVENTS], lfd, acceptConn, &g_events[MAX_EVENTS]);
	eventAdd(efd, EPOLLIN, &g_events[MAX_EVENTS]);
}

int main(int argc, char *argv[])
{
	unsigned short port = SERV_PORT;

	if(2 == argc)
		port = atoi(argv[1]);

	g_efd = epoll_create(MAX_EVENTS + 1);
	if(g_efd <= 0)
		printf("create efd in %s err %s\n", __func__, strerror(errno));

	initListenSocket(g_efd, port);

	struct epoll_event events[MAX_EVENTS + 1];
	printf("server running: port[%d]\n", port);

	int checkPos = 0, i;
	while(1)
	{
		//超时验证，每次测试100个链接，不测试listenfd
		//当客户端60秒内没有和服务器通信，则关闭此客户端链接
		long now = time(NULL);
		//一次循环检测100个，使用checkPos控制检测对象
		for(i = 0; i < 100; i++, checkPos++)
		{
			if(checkPos == MAX_EVENTS)
				checkPos = 0;
			if(g_events[checkPos].status != 1)
				continue;

			long duration = now - g_events[checkPos].last_active;

			if(duration >= 60)
			{
				close(g_events[checkPos].fd);
				printf("[fd=%d] timeout\n", g_events[checkPos].fd);
				eventDel(g_efd, &g_events[checkPos]);
			}
		}
		//等待事件发生
		int nfd = epoll_wait(g_efd, events, MAX_EVENTS + 1, 1000);
		if(nfd < 0)
		{
			printf("epoll_wait error, exit\n");
			break;
		}

		for(i = 0; i < nfd; i++)
		{
			struct myEvent_s *ev = (struct myEvent_s *)events[i].data.ptr;
			//读就绪事件
			if((events[i].events & EPOLLIN) && (ev->events & EPOLLIN))
				ev->call_back(ev->fd, events[i].events, ev->arg);
			//写就绪事件
			if((events[i].events & EPOLLOUT) && (ev->events & EPOLLOUT))
				ev->call_back(ev->fd, events[i].events, ev->arg);
		}
	}

	return 0;
}
