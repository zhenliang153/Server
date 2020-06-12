#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#define MAXLINE 	80
#define SERV_PORT	8000

int main(int argc, char *argv[])
{
	int i, maxi, maxfd, listenfd, connfd, sockfd;
	int nready, client[FD_SETSIZE];	//FD_SETSIZE 默认为 1024
	ssize_t n;
	fd_set rset, allset;
	char buf[MAXLINE];
	char str[INET_ADDRSTRLEN];	//#define INET_ADDRSTRLEN 16
	socklen_t cliaddr_len;
	struct sockaddr_in cliaddr, servaddr;

	listenfd = socket(AF_INET, SOCK_STREAM, 0);

	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port = htons(SERV_PORT);

	bind(listenfd, (struct sockaddr *)&servaddr, sizeof(servaddr));

	//默认最大128
	listen(listenfd, 20);

	maxfd = listenfd;		//初始化
	maxi = -1;			//client[]的下标

	for (i = 0; i < FD_SETSIZE; i++)
		client[i] = -1;		//用-1初始化client[]

	FD_ZERO(&allset);
	FD_SET(listenfd, &allset);	//构造select监控文件描述符集

	for ( ; ; ) 
	{
		rset = allset;		//每次循环时都从新设置select监控信号集
		nready = select(maxfd+1, &rset, NULL, NULL, NULL);

		if (nready < 0)
		{
			//除了函数调用出错，信号中断也会返回-1，所以务必要考虑到这种情况
			if(errno == EINTR)
			{
				printf("connecting interruptted by signal, try again!\n");
				continue;
			}
			perror("select error");
			exit(1);
		}
		else if (nready == 0)
		{
			printf("select timeout!\n");
			continue;
		}
		if (FD_ISSET(listenfd, &rset)) 
		{ 
			//new client connection
			cliaddr_len = sizeof(cliaddr);
			connfd = accept(listenfd, (struct sockaddr *)&cliaddr, &cliaddr_len);
			if(connfd == -1)
			{
				printf("accept error!\n");
				break;
			}
			printf("received from %s at PORT %d\n",
					inet_ntop(AF_INET, &cliaddr.sin_addr, str, sizeof(str)),
					ntohs(cliaddr.sin_port));
			for (i = 0; i < FD_SETSIZE; i++) 
			{
				if (client[i] < 0) 
				{
					//保存accept返回的文件描述符到client[]里
					client[i] = connfd;
					break;
				}
			}
			//达到select能监控的文件个数上线1024
			if (i == FD_SETSIZE) 
			{
				fputs("too many clients\n", stderr);
				exit(1);
			}
			//添加一个新的文件描述符到监控信号集里
			FD_SET(connfd, &allset);
			if (connfd > maxfd)
				maxfd = connfd;	//select第一个参数需要
			if (i > maxi)
				maxi = i;	//更新client[]最大下标值
			//如果没有更多的就绪文件描述符继续回到上面select阻塞监听，负责处理未处理完的就绪文件描述符
			if (--nready == 0)
				continue;
		}
		for (i = 0; i <= maxi; i++) 
		{
			//检测哪个client有数据就绪
			if ( (sockfd = client[i]) < 0)
				continue;
			if (FD_ISSET(sockfd, &rset))
			{
				if ( (n = read(sockfd, buf, MAXLINE)) == 0) 
				{
					//当client关闭链接时，服务器端也关闭对应链接
					close(sockfd);
					//解除select监控此文件描述符
					FD_CLR(sockfd, &allset);
					client[i] = -1;
				} 
				else if(n == -1)
				{
					//除了被信号中断的情形，其他情况都是出错
					if(errno != EINTR)
					{
						printf("read error!\n");
						break;
					}
				}
				else 
				{
					int j;
					for (j = 0; j < n; j++)
						buf[j] = toupper(buf[j]);
					write(sockfd, buf, n);
				}
				if (--nready == 0)
					break;
			}
		}
	}
	close(listenfd);
	return 0;
}
