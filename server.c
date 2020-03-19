#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>

#define SERV_PORT 8000

int main(void)
{
	int lfd, cfd;
	struct sockaddr_in serv_addr, cli_addr;
	socklen_t cli_len;
	char buf[1024];
	int len;
	int ret;

	//AF_INET:ipv4	SOCK_STREAM:stream	0:tcp
	lfd = socket(AF_INET, SOCK_STREAM, 0);
	
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(SERV_PORT);

	//server端处于TIME_WAIT状态时，需等待2MSL时间重启server，否则报"Address already in use"
	//可通过设置“SO_REUSEADDR”选项确保连接成功
	//setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR,(const void *)&reuse , sizeof(int));
	ret = bind(lfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
	if(ret < 0) {
		perror("bind addr");
		_exit(-1);
	}

	ret = listen(lfd, 128);
	if (ret < 0) {
		perror("listen");
		_exit(-1);
	}

	cli_len = sizeof(cli_addr);
	cfd = accept(lfd, (struct sockaddr *)&cli_addr, &cli_len);
	if (cfd < 0) {
		printf("accept:%d %s", getpid(), strerror(errno));
		_exit(-1);
	}
	while(1) {
		len = read(cfd, buf, sizeof(buf));
		write(STDOUT_FILENO, buf, len);
		write(STDOUT_FILENO, buf, len);
		write(cfd, buf, len);
		write(cfd, buf, len);
		printf("process:%d close socket\n", getpid());
	}

	close(lfd);
	close(cfd);

	return 0;
}
