#include "Server.h"


Server::Server()
{
}

Server::~Server()
{
}

bool Server::init(int port, Config* config)
{
	this->config = config;
	
	int svrfd = socket(AF_INET,SOCK_STREAM,0);
	if(svrfd==-1)
		return false;
	//绑定地址
	struct sockaddr_in svraddr;
	memset(&svraddr,0,sizeof(svraddr));
	svraddr.sin_family = AF_INET;
	svraddr.sin_addr.s_addr = htonl(INADDR_ANY);
	svraddr.sin_port=htons(port);
	int opt = 1;
	setsockopt(svrfd, SOL_SOCKET,
			SO_REUSEADDR, (const void *) &opt, sizeof(opt));
	if(bind(svrfd,(sockaddr*)&svraddr,sizeof(svraddr))==-1)
	{
		close(svrfd);
		return false;
	}
	
	// 监听端口 最大队列20
	listen(svrfd,20);
	
	if(setnonblocking(svrfd)==-1)
	{
		close(svrfd);
		return false;
	}
	// 创建EPOLL监听
	int epfd = epoll_create(1024);
	if(epfd==-1)
	{
		close(svrfd);
		return false;
	}
	struct epoll_event ev;
	ev.events = EPOLLIN | EPOLLET;
	ev.data.ptr = new SockInfo(svrfd,SockServer);
	if(epoll_ctl(epfd,EPOLL_CTL_ADD,svrfd,&ev)==-1)
	{
		close(svrfd);
		close(epfd);
		return false;
	}else{
		this->epfd = epfd;
	}
	
	return true;
}
int Server::loop()
{
	struct epoll_event evs[32];
	for(;;)
	{
		int evc = epoll_wait(this->epfd,evs,sizeof(evs)/sizeof(evs[0]),-1);
		if(evc==-1)
		{
			// 屏蔽高层打断
			if(errno==EAGAIN)
				continue;
			return -1;
		}
		int i;
		for(i=0;i<evc;++i)
		{
			SockInfo* info = ((SockInfo*)evs[i].data.ptr);
			// 判断触发事件的socket类型
			ServerArg* arg = (ServerArg*)malloc(sizeof(ServerArg));
			arg->server = this;
			arg->info = info;
			pthread_t pid; // 线程ID
			switch(info->getType())
			{
				case SockServer://如果是服务器accept
					//pthread_create(&pid, NULL, acceptClient, arg);  
					acceptClient(arg);
					break;
				default:
					char c=0;
					if(recv(info->getFd(),&c,1,MSG_PEEK)!=1)
					{
						//pthread_create(&pid, NULL, destorySock, arg);  
						destorySock(arg);
					}else{
						switch(info->getType())
						{
							case SockUp:
								
								pthread_create(&pid, NULL, forwardUp, arg);  
								//forwardUp(arg);
								break;
							case SockDown:
								pthread_create(&pid, NULL, forwardDown, arg);  
								//forwardDown(arg);
								break;
							default:
								break;
						}
					}
					break;
			}
			// 其他Socket可读
		}/* 事件i列表循环 */
	}/* epoll死循环 */
	
	return 0;
}

void* Server::acceptClient(void* arg)
{
	SockInfo *info = ((ServerArg*)arg)->info;
	Server *server = ((ServerArg*)arg)->server;
	free(arg);
	int fd = info->getFd();
	int client;
	struct sockaddr_in clientaddr;
	socklen_t clientlen;
	while((client=accept(fd,(sockaddr *)&clientaddr,&clientlen))!=-1)
	{
		struct epoll_event ev;
		ev.events = EPOLLIN | EPOLLET;
		ev.data.ptr = new SockInfo(client,SockUp);
		if(setnonblocking(client)==-1)
		{
			close(client);
			continue;
		}
		if(epoll_ctl(server->epfd,EPOLL_CTL_ADD,client,&ev)==-1)
		{
			close(client);
			continue;
		}
	}
	return NULL;
}

void* Server::forwardDown(void* arg)
{
	SockInfo *info = ((ServerArg*)arg)->info;
	//Server *server = ((ServerArg*)arg)->server;
	free(arg);
	if(info->getBorther()!=NULL)
	{
		int src = info->getFd();
		int dest = info->getBorther()->getFd();
		int len;
		char buf[1024*32];
		while((len=recv(src,buf,sizeof(buf),0))>0)
		{
			send(dest,buf,len,0);
		}
	}
	return NULL;
}

void* Server::forwardUp(void* arg)
{
	SockInfo *info = ((ServerArg*)arg)->info;
	Server *server = ((ServerArg*)arg)->server;
	free(arg);
	int src=info->getFd();
	char buf[1024*16];
	int len;
	while((len=recv(src,buf,sizeof(buf),0))>0)
	{
		buf[len]=0;
		// 如果连接了目标
		if(info->getBorther()==NULL)
		{
			//没有连接目标
			std::string addr;
			if(strncmp(buf,"GET",3)==0 || strncmp(buf,"POST",4)==0)
			{
				// HTTP连接
				addr=server->config->getValue("HTTP代理");
			}
			else if(strncmp(buf,"CONNECT",7)==0)
			{
				// HTTPS连接
				addr=server->config->getValue("HTTPS代理");
			}
			if(!addr.empty())
			{
				std::string ip =addr.substr(0,addr.find_first_of(":"));
				std::string port =addr.substr(addr.find_last_of(":")+1,addr.length());

				int nfd;
				if((nfd=socket(AF_INET,SOCK_STREAM,0))!=-1)
				{
					struct sockaddr_in dest;
					memset(&dest,0,sizeof(dest));
					dest.sin_family = AF_INET;
					dest.sin_port = htons(atoi(port.c_str()));
					dest.sin_addr.s_addr = inet_addr(ip.c_str());
					if(-1 != connect(nfd,(struct sockaddr*)&dest,sizeof(struct sockaddr)))
					{
						if(-1!=setnonblocking(nfd))
						{
							struct epoll_event ev;
							ev.events = EPOLLIN | EPOLLET;
							ev.data.ptr = new SockInfo(nfd,SockDown,info);
							if(epoll_ctl(server->epfd,EPOLL_CTL_ADD,nfd,&ev)!=-1)
							{
								std::cout<<"服务器连接成功"<<std::endl;
							}else{
								close(nfd);
							}
						}else{
							close(nfd);
						}
					}else{
						close(nfd);
					}
				}
			}/* 连接 */
		}/* 未连接 */
		if(info->getBorther()!=NULL)
		{
			if(strncmp(buf,"GET",3)==0 || strncmp(buf,"POST",4)==0|| strncmp(buf,"CONNECT",7)==0)
			{
				std::string header(buf);
				server->config->exec("HTTP",header);
				send(info->getBorther()->getFd(),header.c_str(),header.length(),0);
			}else{
				send(info->getBorther()->getFd(),buf,len,0);
			}
		}
	}/* 接收循环 */
	return NULL;
}
void* Server::destorySock(void* arg)
{
	SockInfo *info = ((ServerArg*)arg)->info;
	Server *server = ((ServerArg*)arg)->server;
	free(arg);
	if(info->getBorther()!=NULL)
	{
		info->getBorther()->setBorther(NULL);
		ServerArg* narg = (ServerArg*)malloc(sizeof(ServerArg));
			narg->server = server;
			narg->info = info->getBorther();
		server->destorySock(narg);
	}
	struct epoll_event ev;
	ev.events = EPOLLIN | EPOLLET;
	epoll_ctl(server->epfd,EPOLL_CTL_DEL,info->getFd(),&ev);
	close(info->getFd());
	
	return NULL;
}