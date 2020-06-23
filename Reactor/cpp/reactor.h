#pragma once

#include <list>
#include <thread>
#include <mutex>
#include <condition_variable>

#define WORKER_THREAD_NUM 5

class Reactor {
public:
	Reactor();
	~Reactor();

	bool init(unsigned int ip, short port);
	bool uninit();

	bool close_client(int clientfd);

	static void* main_loop(void* p);

private:
	Reactor(const Reactor& reactor) = delete;
	Reactor& operator=(const Reactor& reactor) = delete;

	bool create_server_listener(unsigned int ip, short port);
	
	static void accept_thread_proc(Reactor* reactor);
	static void worker_thread_proc(Reactor* reactor);

private:
	int m_listenfd = 0;
	int m_epollfd = 0;
	bool m_isStop = false;

	std::shared_ptr<std::thread> m_acceptthread;
	std::shared_ptr<std::thread> m_workerthreads[WORKER_THREAD_NUM];

	std::condition_variable m_acceptcond;
	std::mutex	m_acceptmutex;

	std::condition_variable m_workercond;
	std::mutex m_workermutex;

	std::list<int> m_listClients;
};
