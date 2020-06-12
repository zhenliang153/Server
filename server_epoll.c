#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <errno.h>

#define	SERV_PORT	8000
#define	OPEN_MAX	1024

void perr_exit(const char* str)
{
	perror(str);
	exit(1);
}

int main(void)
{
	int lfd, cfd, sfd, efd;
	struct sockaddr_in serv_addr, cli_addr;
	socklen_t cli_len;
	char buf[OPEN_MAX], str[INET_ADDRSTRLEN];
	int len, nReady, res, i, j;
	struct epoll_event tep, ep[OPEN_MAX];

	//AF_INET:ipv4	SOCK_STREAM:stream	0:tcp
	lfd = socket(AF_INET, SOCK_STREAM, 0);

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(SERV_PORT);

	bind(lfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));

	listen(lfd, 128);

	efd = epoll_create(OPEN_MAX);
	if(-1 == efd)
		perr_exit("epoll_create");

	tep.events = EPOLLIN;
	tep.data.fd = lfd;

	res = epoll_ctl(efd, EPOLL_CTL_ADD, lfd, &tep);
	if(-1 == res)
		perr_exit("epoll_ctl");

	for(;;)
	{
		//阻塞监听
		nReady = epoll_wait(efd, ep, OPEN_MAX, -1);
		if(-1 == nReady)
		{
			if(errno == EINTR)
			{
				printf("connecting interruptted by signal, try again!\n");
				continue;
			}
			perr_exit("epoll_wait");
		}
		else if(0 == nReady)
		{
			printf("epoll timeout\n");
			continue;
		}
		for(i = 0; i < nReady; i++)
		{
			//有新的客户端发出连接请求
			if(ep[i].data.fd == lfd)
			{
				cli_len = sizeof(cli_addr);
				cfd = accept(lfd, (struct sockaddr *)&cli_addr, &cli_len);
				if(cfd == -1)
				{
					printf("accept error!\n");
					break;
				}
				printf("received from %s at PORT %d\n", 
						inet_ntop(AF_INET, &cli_addr.sin_addr, str, sizeof(str)),
						ntohs(cli_addr.sin_port));
				tep.events = EPOLLIN;
				tep.data.fd = cfd;
				res = epoll_ctl(efd, EPOLL_CTL_ADD, cfd, &tep);
				if(-1 == res)
					perr_exit("epoll_ctl");
			}
			else 
			{
				sfd = ep[i].data.fd;
				len = read(sfd, buf, sizeof(buf));
				if(-1 == len)
				{
					if(errno != EWOULDBLOCK && errno != EINTR)
					{
						if(epoll_ctl(efd, EPOLL_CTL_DEL, ep[i].data.fd, NULL) != -1)
						{
							printf("client disconnected,clientfd: %d\n", ep[i].data.fd);
						}
						close(ep[i].data.fd);
					}
				}
				else if(0 == len)
				{
					//表示客户端关闭
					res = epoll_ctl(efd, EPOLL_CTL_DEL, sfd, NULL);
					close(sfd);
					printf("client[%d] closed connection\n", j);
				}
				else
				{
					for(j = 0; j < len; j++)
						buf[j] = toupper(buf[j]);
					write(sfd, buf, len);
				}
			}
		}
		write(STDOUT_FILENO, buf, len);
	}
	close(lfd);
	close(cfd);

	return 0;
}
