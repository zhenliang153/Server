#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define SERV_PORT 8000

int main(void)
{
	int lfd, cfd;
	struct sockaddr_in serv_addr, cli_addr;
	socklen_t cli_len;
	char buf[1024];
	int len;

	//AF_INET:ipv4	SOCK_STREAM:stream	0:tcp
	lfd = socket(AF_INET, SOCK_STREAM, 0);
	
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(SERV_PORT);

	bind(lfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));

	listen(lfd, 128);

	cli_len = sizeof(cli_addr);
	cfd = accept(lfd, (struct sockaddr *)&cli_addr, &cli_len);
	
	len = read(cfd, buf, sizeof(buf));
	write(STDOUT_FILENO, buf, len);
	write(STDOUT_FILENO, buf, len);

	close(lfd);
	close(cfd);

	return 0;
}
