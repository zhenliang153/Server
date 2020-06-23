#include <iostream>
#include <chrono>
#include <iomanip>

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/epoll.h>

#include "reactor.h"

Reactor::Reactor() {
}

Reactor::~Reactor() {
}

bool Reactor::init(unsigned int ip, short port) {
	if(!create_server_listener(ip, port)) {
		std::cout << "Unable to bind: " << ip << ":" << port << std::endl;
		return false;
	}
	std::cout << "main thread id = " << std::this_thread::get_id() << std::endl;
	
	//启动接受新连接的线程
	m_acceptthread.reset(new std::thread(Reactor::accept_thread_proc, this));

	//启动工作线程
	for(auto& t: m_workerthreads) {
		t.reset(new std::thread(Reactor::worker_thread_proc, this));
	}
	return true;
}

bool Reactor::uninit() {
	m_isStop = true;
	m_acceptcond.notify_one();
	m_workercond.notify_all();
	
	m_acceptthread->join();
	for(auto& t: m_workerthreads) {
		t->join();
	}
	epoll_ctl(m_epollfd, EPOLL_CTL_DEL, m_listenfd, NULL);

	shutdown(m_listenfd, SHUT_RDWR);
	close(m_listenfd);
	close(m_epollfd);

	return true;
}

bool Reactor::close_client(int clientfd) {
	if(epoll_ctl(m_epollfd, EPOLL_CTL_DEL, clientfd, NULL) == -1) {
		std::cout << "epoll_ctl error! release client failed!" << std::endl;
	}
	return true;
}

void* Reactor::main_loop(void* p) {
	std::cout << "main thread id = " << std::this_thread::get_id() << std::endl;
	Reactor* pReactor = static_cast<Reactor*>(p);
	struct epoll_event ev[1024];
	while(!pReactor->m_isStop) {
		int n = epoll_wait(pReactor->m_epollfd, ev, 1024, 10);
		if(n == 0) {
			continue;
		} else if(n < 0) {
			std::cout << "epoll_wait error!" << std::endl;
			continue;
		}
		n = std::min(n, 1024);
		for(int i = 0; i < n; i++) {
			if(ev[i].data.fd == pReactor->m_listenfd) {
				pReactor->m_acceptcond.notify_one();
			} else {
				{
					std::unique_lock<std::mutex> guard(pReactor->m_workermutex);
					pReactor->m_listClients.push_back(ev[i].data.fd);
				}
				pReactor->m_workercond.notify_one();
			}
		}
	}
	std::cout << "main loop exit ..." << std::endl;
	return NULL;
}

void Reactor::accept_thread_proc(Reactor* pReactor) {
	std::cout << "accept thread, thread_id = " << std::this_thread::get_id() << std::endl;
	while(true) {
		struct sockaddr_in clientaddr;
		socklen_t addrlen;
		int newfd;
		{
			std::unique_lock<std::mutex> guard(pReactor->m_acceptmutex);
			pReactor->m_acceptcond.wait(guard);
			if(pReactor->m_isStop) {
				break;
			}
			newfd = accept(pReactor->m_listenfd, (struct sockaddr*)&clientaddr, &addrlen);
		}
		if(newfd == -1) {
			continue;
		}
		std::cout << "new client connected:" << inet_ntoa(clientaddr.sin_addr) <<
					":" << ntohs(clientaddr.sin_port) << std::endl;
		//将新socket设置为非阻塞
		int oldflag = fcntl(newfd, F_GETFL, 0);
		int newflag = oldflag | O_NONBLOCK;
		if(fcntl(newfd, F_SETFL, newflag) == -1) {
			std::cout << "fcntl error, oldflag = " << oldflag << ", newflag = " << newflag << std::endl;
			continue;
		}
		struct epoll_event ep;
		memset(&ep, 0, sizeof(ep));
		ep.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
		ep.data.fd = newfd;
		if(epoll_ctl(pReactor->m_epollfd, EPOLL_CTL_ADD, newfd, &ep) == -1) {
			std::cout << "epoll_ctl error, fd = " << newfd << std::endl; 
		}
	}
	std::cout << "accept thread exit ..." << std::endl;
}

void Reactor::worker_thread_proc(Reactor* pReactor) {
	std::cout << "new worker thread, thread_id = " << std::this_thread::get_id() << std::endl;
	std::string strclientmsg;
	strclientmsg.reserve(2048);
	char buf[256];
	while(true) {
		int clientfd;
		{
			std::unique_lock<std::mutex> guard(pReactor->m_workermutex);
			while(pReactor->m_listClients.empty()) {
				//外部相关信号
				if(pReactor->m_isStop) {
					std::cout << "worker thread exit ..." << std::endl;
					return;
				}
				pReactor->m_workercond.wait(guard);
			}
			clientfd = pReactor->m_listClients.front();
			pReactor->m_listClients.pop_front();
		}
		//时间标签共21个字符
		strclientmsg.resize(21);
		bool isError = false;
		while(true) {
			memset(buf, 0, sizeof(buf));
			int n = recv(clientfd, buf, 256, 0); 
			if(n == -1) {
				if(errno != EWOULDBLOCK) {
					std::cout << "recv error! client disconnected, fd = " << clientfd << std::endl;
					pReactor->close_client(clientfd);
					isError = true;
				}
				break;
			} else if(n == 0) {
				std::cout << "client disconnected, fd = " << clientfd << std::endl;
				pReactor->close_client(clientfd);
				isError = true;
				break;
			}
			strclientmsg.append(buf);
		}
		if(isError) {
			continue;
		}
		//将消息加上时间标签后发回
		auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
		std::stringstream ss;
		ss << std::put_time(std::localtime(&t), "%Y-%m-%d %H:%M:%S");
		strclientmsg[0] = '[';
		strclientmsg.replace(1, 19, ss.str(), 0, 19);
		strclientmsg[20] = ']';
		std::cout << "client msg: " << strclientmsg;

		size_t offset = 0;
		while(true) {
			int n = send(clientfd, strclientmsg.c_str() + offset, strclientmsg.length() - offset, 0);
			if(n == -1) {
				if(errno == EWOULDBLOCK) {
					std::this_thread::sleep_for(std::chrono::milliseconds(10));
					continue;
				} else {
					std::cout << "send error, fd = " << clientfd << std::endl;
					pReactor->close_client(clientfd);
					break;
				}
			}
			std::cout << "send msg: " << strclientmsg.substr(offset, n);
			offset += n;
			if(offset == strclientmsg.length()) {
				break;
			}
		}
	}
}

bool Reactor::create_server_listener(unsigned int ip, short port) {
	//非阻塞socket
	if((m_listenfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)) == -1) {
		return false;
	}
	int on = 1;
	setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, (char*)&on, sizeof(on));
	setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEPORT, (char*)&on, sizeof(on));
		
	struct sockaddr_in servaddr;
	memset(&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port = htons(port);
	if(bind(m_listenfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) == -1) {
		std::cout << "bind error!" << std::endl;
		return false;
	}
	if(listen(m_listenfd, 50) == -1) {
		std::cout << "listen error!" << std::endl;
		return false;
	}
	if((m_epollfd = epoll_create(1)) == -1) {
		std::cout << "epoll_create error!" << std::endl;
		return false;
	}
	struct epoll_event ep;
	memset(&ep, 0, sizeof(ep));
	ep.events = EPOLLIN | EPOLLRDHUP;
	ep.data.fd = m_listenfd;
	if(epoll_ctl(m_epollfd, EPOLL_CTL_ADD, m_listenfd, &ep) == -1) {
		std::cout << "epoll_ctl error!" << std::endl;
		return false;
	}
	std::cout << "listen port: " << port << std::endl;
	return true;
}
