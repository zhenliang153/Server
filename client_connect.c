#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>

#define SELECT
#ifndef SELECT
#include <poll.h>
#endif

/*
 *异步的 connect 技术，或者叫非阻塞的 connect
 *1.创建socket，并将socket设置为非阻塞模式
 *2.调用connect函数，此时无论connect是否连接成功会立即返回；
 *	如果返回-1并不一定表示连接出错，
 *	如果测试错误码EINPROGRESS，则表示正在尝试连接
 *3.接着调用select函数，在指定时间内判断该socket是否可写
 *  如果可写说明连接成功，反之则认为连接失败
 * */

#define SERV_ADDR "127.0.0.1"
#define SERV_PORT 8000
#define SENV_DATA "hello world"


int main(void) {
	//1.创建socket
	int cfd = socket(AF_INET, SOCK_STREAM, 0);
	if(cfd == -1) {
		printf("create client socket error!\n");
		return -1;
	}
	//将cfd设为非阻塞模式
	int old_flag = fcntl(cfd, F_GETFL, 0);
	int new_flag = old_flag | O_NONBLOCK;
	if(fcntl(cfd, F_SETFL, new_flag) == -1) {
		close(cfd);
		printf("set socket to nonblock error!\n");
		return -1;
	}
	//2.连接服务器
	struct sockaddr_in serveraddr;
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = inet_addr(SERV_ADDR);
	serveraddr.sin_port = htons(SERV_PORT);
	for(;;) {
		int ret = connect(cfd, (struct sockaddr*)&serveraddr, sizeof(serveraddr));
		if(ret == 0) {
			printf("conect to server ok!\n");
			close(cfd);
			return 0;
		} else if(ret == -1) {
			if(errno == EINTR) {
				//connect动作被信号中断，重试connect
				printf("connect interruptted by signal, try again!\n");
				continue;
			} else if(errno == EINPROGRESS) {
				//连接正在尝试中
				break;
			} else {
				//真的出错了
				close(cfd);
				return -1;
			}
		}
	}

	//3.调用select或poll函数，检测是否可写
#ifdef SELECT
	fd_set writeset;
	FD_ZERO(&writeset);
	FD_SET(cfd, &writeset);
	struct timeval tv;
	tv.tv_sec = 3;
	tv.tv_usec = 0;
	//调用select函数，检测是否可写
	if(select(cfd+1, NULL, &writeset, NULL, &tv) == 1) {
		printf("[select]connect to server ok!\n");
	} else {
		printf("[select]connect to server error!\n");
	}
#else
	struct pollfd event;
	event.fd = cfd;
	event.events = POLLOUT;
	int timeout = 3000;
	//调用poll函数，检测是否可写
	if(poll(&event, 1, timeout) != 1) {
		printf("[poll]connect to server error!\n");
		close(cfd);
		return -1;
	}
	if(!(event.events & POLLOUT)) {
		printf("[POLLOUT]connect to server error!\n");
		close(cfd);
		return -1;
	}
	printf("[poll]connect to server ok!\n");
#endif

	/*
	*在Linux系统上一个socket没有建立连接之前，用select函数检测其是否可写，也会得到可写的结果。所以
	*connect之后，不仅要用select检测可写，还要检测此时socket是否出错，通过错误码来检测确定是否连接上。
	*错误码为 0 表示连接上，反之为未连接上。
	* */
	int err;
	socklen_t len = (socklen_t)(sizeof(err));
	//4.通过错误码检测确定是否连接上
	if(getsockopt(cfd, SOL_SOCKET, SO_ERROR, &err, &len) < 0) {
		close(cfd);
		return -1;
	}
	if(err == 0) {
		printf("connect to server ok!\n");
	} else {
		printf("connect to server error!\n");
	}
	close(cfd);
	return 0;
}
