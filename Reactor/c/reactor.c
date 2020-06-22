#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <errno.h>

#define SERV_PORT 8000
#define WORKER_THREAD_NUM 5

int g_listenfd = 0;
int g_epollfd = 0;
int g_stop = 0;

pthread_t g_acceptthreadid = 0;
pthread_t g_threadid[WORKER_THREAD_NUM] = { 0 };

pthread_cond_t g_cond;
pthread_mutex_t g_mutex;
pthread_cond_t g_acceptcond;
pthread_mutex_t g_acceptmutex;
pthread_mutex_t g_clientmutex;

/********************************************/
//自定义list，从头部插入，尾部移除
typedef struct Node_ {
	int fd;
	struct Node_* pre;
	struct Node_* next;
} Node;

typedef struct ClientList_ {
	Node* head;
	Node* tail;
	int empty;
} ClientList;

ClientList* list = NULL;

Node* createNode(int fd) {
	Node* node =  (Node*)malloc(sizeof(Node));
	node->fd = fd;
	node->pre = NULL;
	node->next = NULL;
	return node;
}

void push_front(int fd) {
	Node* node = createNode(fd);
	if(list->empty == 0) {
		list->head = list->tail = node;
		list->empty = 1;
	} else {
		node->next = list->head;
		list->head->pre = node;
		list->head = node;
	}
}

void pop_back() {
	if(list->empty == 0) {
		printf("list empty!\n");
		return;
	}
	Node* node = list->tail;
	if(node->pre == NULL) {
		list->head = list->tail = NULL;
		list->empty = 0;
	} else {
		list->tail = node->pre;
		list->tail->next = NULL;
	}
	free(node);
	node = NULL;
}

int back() {
	if(list->tail == NULL) {
		return -1;
	}
	return list->tail->fd;
}
/********************************************/
char strclientmsg[2048];
int msglen = 0;
/********************************************/

//创建守护进程
void daemon_run() {
	//子进程退出时给父进程的信号
	signal(SIGCHLD, SIG_IGN);
	//<0 创建子进程失败
	//=0 返回新创建的子进程
	//>0 返回父进程
	int pid = fork();
	if(pid < 0) {
		printf("fork error!\n");
		exit(-1);
	} else if(pid > 0) {
		//父进程退出
		exit(0);
	}
	//创建新的会话
	setsid();
	int fd = open("/", O_RDWR, 0);
	if(fd != -1) {
		dup2(fd, STDIN_FILENO);
		dup2(fd, STDOUT_FILENO);
		dup2(fd, STDERR_FILENO);
	}
	if(fd > 2) {
		close(fd);
	}
}

int create_server_listener(unsigned int ip, short port) {
	//非阻塞socket
	if((g_listenfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)) == -1) {
		printf("socket error!\n");
		return -1;
	}
	int on = 1;
	setsockopt(g_listenfd, SOL_SOCKET, SO_REUSEADDR, (char*)&on, sizeof(on));
	setsockopt(g_listenfd, SOL_SOCKET, SO_REUSEPORT, (char*)&on, sizeof(on));
		
	struct sockaddr_in servaddr;
	memset(&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	//servaddr.sin_addr.s_addr = htonl(AF_INET);
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port = htons(port);
	if(bind(g_listenfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) == -1) {
		printf("bind error!\n");
		return -1;
	}
	if(listen(g_listenfd, 50) == -1) {
		printf("listen error!\n");
		return -1;
	}
	if((g_epollfd = epoll_create(1)) == -1) {
		printf("epoll_create error!\n");
		return -1;
	}
	struct epoll_event ep;
	memset(&ep, 0, sizeof(ep));
	//ep.events = EPOLLIN | EPOLLRDHUP;
	ep.events = EPOLLIN;// | EPOLLRDHUP;
	ep.data.fd = g_listenfd;	
	if(epoll_ctl(g_epollfd, EPOLL_CTL_ADD, g_listenfd, &ep) == -1) {
		printf("epoll_ctl error!\n");
		return -1;
	}
	printf("listen port: %hd\n", port);
	return 0;
}

void prog_exit(int signo) {
	signal(SIGINT, SIG_IGN);
	signal(SIGKILL, SIG_IGN);
	signal(SIGTERM, SIG_IGN);
	printf("program recv signal %d to exit.\n", signo);
	g_stop = 1;
	epoll_ctl(g_epollfd, EPOLL_CTL_DEL, g_listenfd, NULL);
	shutdown(g_listenfd, SHUT_RDWR);
	close(g_listenfd);
	close(g_epollfd);

	pthread_cond_destroy(&g_acceptcond);
	pthread_mutex_destroy(&g_acceptmutex);

	pthread_cond_destroy(&g_cond);
	pthread_mutex_destroy(&g_mutex);

	pthread_mutex_destroy(&g_clientmutex);
}

void* accept_thread_func(void* arg) {
	while(g_stop == 0) {
		pthread_mutex_lock(&g_acceptmutex);
		pthread_cond_wait(&g_acceptcond, &g_acceptmutex);

		struct sockaddr_in clientaddr;
		socklen_t addrlen;
		int newfd = accept(g_listenfd, (struct sockaddr*)&clientaddr, &addrlen);
		pthread_mutex_unlock(&g_acceptmutex);
		if(newfd == -1) {
			continue;
		}
		printf("new client connected: %s:%hd\n", inet_ntoa(clientaddr.sin_addr),
													ntohs(clientaddr.sin_port));
		//将新socket设置为非阻塞
		int oldflag = fcntl(newfd, F_GETFL, 0);
		int newflag = oldflag | O_NONBLOCK;
		if(fcntl(newfd, F_SETFL, newflag) == -1) {
			printf("fcntl error, oldflag = %d, newflag = %d\n.", oldflag, newflag);
			continue;
		}
		struct epoll_event ep;
		memset(&ep, 0, sizeof(ep));
		//ep.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
		ep.events = EPOLLIN | EPOLLET;
		ep.data.fd = newfd;
		if(epoll_ctl(g_epollfd, EPOLL_CTL_ADD, newfd, &ep) == -1) {
			printf("epoll_ctl error, fd = %d.", newfd);
		}
	}
	return NULL;
}

void release_client(int clientfd) {
	if(epoll_ctl(g_epollfd, EPOLL_CTL_DEL, clientfd, NULL) == -1) {
		printf("epoll_ctl error! release client failed!\n");
	}
	close(clientfd);
}

void* worker_thread_func(void* arg) {
	while(g_stop == 0) {
		pthread_mutex_lock(&g_clientmutex);
		while(list->empty == 0) {
			pthread_cond_wait(&g_cond, &g_clientmutex);
		}
		int clientfd = back();
		pop_back();
		pthread_mutex_unlock(&g_clientmutex);
		
		char buf[256];
		int isError = 0;
		//memset(strclientmsg, 0, sizeof(strclientmsg));
		//时间标签共21个字符
		msglen = 21;
		while(1) {
			memset(buf, 0, sizeof(buf));
			int n = recv(clientfd, buf, 256, 0);
			if(n == -1) {
				if(errno != EWOULDBLOCK) {
					printf("recv error! client disconnected, fd = %d\n", clientfd);
					release_client(clientfd);
					isError = 1;
				}
				break;
			} else if(n == 0) {
				printf("client disconnected, fd = %d\n", clientfd);
				release_client(clientfd);
				isError = 1;
				break;	
			}
			for(int i = 0; i < n; i++) {
				strclientmsg[msglen++] = buf[i];
			}
		}
		if(isError != 0) {
			continue;
		}
		strclientmsg[msglen] = '\0';
		//将消息加上时间标签后发回
		time_t now = time(NULL);
		struct tm* info = localtime(&now);
		//结尾会写入'\0',缓存并替换一下 
		char c = strclientmsg[21];
		sprintf(strclientmsg, "[%d-%02d-%02d %02d:%02d:%02d]", 1900 + info->tm_year, 1 + info->tm_mon,
							info->tm_mday, info->tm_hour, info->tm_min, info->tm_sec);
		strclientmsg[21] = c;
		printf("client msg: %s", strclientmsg);
		char* msg = strclientmsg;
		while(1) {
			int n = send(clientfd, msg, msglen - (msg - strclientmsg), 0);
			if(n == -1) {
				if(errno == EWOULDBLOCK) {
					sleep(10);
					continue;
				} else {
					printf("send error, fd = %d\n", clientfd);
					release_client(clientfd);
					break;
				}
			}
			printf("send msg: %s", strclientmsg);
			msg += n;
			if(msg - strclientmsg == msglen) {
				break;
			}
		}
	}
	return NULL;
}

int main(int argc, char* argv[]) {
	int ch;
	int daemon_mode = 0;
	short port = 0;
	while((ch = getopt(argc, argv, "p:d")) != -1) {
		switch(ch) {
		case 'd':
			daemon_mode = 1;
			break;
		case 'p':
			port = atol(optarg);
			break;
		}
	}
	if(daemon_mode != 0) {
		daemon_run();
	}
	if(port == 0) {
		port = SERV_PORT;
	}
	if(create_server_listener(INADDR_ANY, port) != 0) {
		printf("unable to create listen server: ip=0.0.0.0, port=%hd.\n",port);
		return -1;
	}
	signal(SIGCHLD, SIG_DFL);
	signal(SIGPIPE, SIG_IGN);
	signal(SIGINT, prog_exit);
	signal(SIGKILL, prog_exit);
	signal(SIGTERM, prog_exit);
	
	pthread_cond_init(&g_acceptcond, NULL);
	pthread_mutex_init(&g_acceptmutex, NULL);

	pthread_cond_init(&g_cond, NULL);
	pthread_mutex_init(&g_mutex, NULL);

	pthread_mutex_init(&g_clientmutex, NULL);

	if(pthread_create(&g_acceptthreadid, NULL, accept_thread_func, NULL) != 0) {
		printf("pthread_create error!\n");
		return -1;
	}

	list = (ClientList*)malloc(sizeof(ClientList));
	list->head = list->tail = NULL;
	list->empty = 0;
	
	for(int i = 0; i < WORKER_THREAD_NUM; i++) {
		pthread_create(&g_threadid[i], NULL, worker_thread_func, NULL);
	}
	struct epoll_event ev[1024];
	while(g_stop == 0) {
		//int n = epoll_wait(g_epollfd, ev, 1024, 10);
		int n = epoll_wait(g_epollfd, ev, 1024, -1);
		if(n == 0) {
			continue;
		} else if(n < 0) {
			printf("epoll_wait error!\n");
			continue;
		}
		if(n > 1024) {
			n = 1024;
		}
		for(int i = 0; i < n; i++) {
			if(ev[i].data.fd == g_listenfd) {
				pthread_cond_signal(&g_acceptcond);
			} else {
				pthread_mutex_lock(&g_clientmutex);
				push_front(ev[i].data.fd);
				pthread_mutex_unlock(&g_clientmutex);
				pthread_cond_signal(&g_cond);
			}
		}
	}
	
	while(list->empty != 0) {
		pop_back();
	}
	free(list);
	list = NULL;

	return 0;
}
