#include <iostream>

#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <netinet/in.h>

#include "reactor.h"

#define SERV_PORT 8000

Reactor g_reactor;

void prog_exit(int signo) {
	std::cout << "program recv signal " << signo << " to exit." << std::endl;
	g_reactor.uninit();
}

//创建守护进程
void daemon_run() {
	//子进程退出时给父进程的信号
	signal(SIGCHLD, SIG_IGN);
	//<0 创建子进程失败
	//=0 返回新创建的子进程
	//>0 返回父进程
	int pid = fork();
	if(pid < 0) {
		std::cout << "fork error!" << std::endl;
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

	if(!g_reactor.init(INADDR_ANY, port)) {
		return -1;
	}

	signal(SIGCHLD, SIG_DFL);
	signal(SIGPIPE, SIG_IGN);
	signal(SIGINT, prog_exit);
	signal(SIGKILL, prog_exit);
	signal(SIGTERM, prog_exit);

	g_reactor.main_loop(&g_reactor);

	return 0;
}
