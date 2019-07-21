#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <errno.h>

#define MAXLINE		80
#define SERV_PORT	8000
#define OPEN_MAX	1024

int main(int argc, char *argv[])
{
	int i, j, maxi, listenfd, connfd, sockfd;
	int nready;
	ssize_t n;
	char buf[MAXLINE], str[INET_ADDRSTRLEN];
	socklen_t clilen;
	struct pollfd client[OPEN_MAX];
	struct sockaddr_in cliaddr, servaddr;

	listenfd = socket(AF_INET, SOCK_STREAM, 0);

	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port = htons(SERV_PORT);

	bind(listenfd, (struct sockaddr *)&servaddr, sizeof(servaddr));

	listen(listenfd, 20);

	client[0].fd = listenfd;
	client[0].events = POLLIN;	//listenfd监听普通读事件 

	for (i = 1; i < OPEN_MAX; i++)
		client[i].fd = -1;	//用-1初始化client[]里剩下元素
	maxi = 0;			//client[]数组有效元素中最大元素下标

	for ( ; ; )
	{
		nready = poll(client, maxi+1, -1);	//阻塞
		if (client[0].revents & POLLIN)
		{
			//有客户端链接请求
			clilen = sizeof(cliaddr);
			connfd = accept(listenfd, (struct sockaddr *)&cliaddr, &clilen);
			printf("received from %s at PORT %d\n",
					inet_ntop(AF_INET, &cliaddr.sin_addr, str, sizeof(str)),
					ntohs(cliaddr.sin_port));
			for (i = 1; i < OPEN_MAX; i++)
			{
				if (client[i].fd < 0)
				{
					//找到client[]中空闲的位置，存放accept返回的connfd
					client[i].fd = connfd;
					break;
				}
			}

			if (i == OPEN_MAX)
			{
				perror("too many clients");
				exit(1);
			}

			client[i].events = POLLIN;	//设置刚刚返回的connfd，监控读事件
			if (i > maxi)
				maxi = i;		//更新client[]中最大元素下标
			if (--nready <= 0)
				continue;		//没有更多就绪事件时，继续回到poll阻塞
		}
		for (i = 1; i <= maxi; i++) 
		{
			//检测client[]
			if ((sockfd = client[i].fd) < 0)
				continue;
			if (client[i].revents & POLLIN)
			{
				if ((n = read(sockfd, buf, MAXLINE)) < 0)
				{
					if (errno == ECONNRESET)
					{
						//当收到RST标志时
						//connection reset by client
						printf("client[%d] aborted connection\n", i);
						close(sockfd);
						client[i].fd = -1;
					} 
					else 
					{
						perror("read error");
						exit(1);
					}
				}
				else if (n == 0)
				{
					printf("client[%d] closed connection\n", i);
					close(sockfd);
					client[i].fd = -1;
				}
				else
				{
					for (j = 0; j < n; j++)
						buf[j] = toupper(buf[j]);
					write(sockfd, buf, n);
				}
				//no more readable descriptors
				if (--nready <= 0)
					break;
			}
		}
	}
	return 0;
}
