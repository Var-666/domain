#include "EpollServer.h"

#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <cerrno>

EpollServer::EpollServer(uint16_t port)
    : port_(port), is_running_(false), listen_fd_(-1), epoll_fd_(-1) {}


EpollServer::~EpollServer() {
    stop();
}

bool EpollServer::setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    // 获取文件描述符状态标志失败
    if(flags == -1){
      std::cerr << "fcntl(F_GETFL) failed, fd: " << fd << ", errno: " << errno << std::endl;
      return false;
    }
    // 设置非阻塞
    if(fcntl(fd,F_SETFL,flags | O_NONBLOCK) == -1){
      std::cerr << "fcntl(F_SETFL) failed, fd: " << fd << ", errno: " << errno << std::endl;
      return false;
    }
    return true;
}

bool EpollServer::createListenSocket() {
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ == -1) {
        std::cerr << "Failed to create socket, errno: " << errno << std::endl;
        return false;
    }

    // 设置端口复用
    int opt = 1;
    if (setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        std::cerr << "setsockopt(SO_REUSEADDR) failed, errno: " << errno << std::endl;
        close(listen_fd_);
        return false;
    }

    // 绑定地址和端口
    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port_);

    if (bind(listen_fd_, (sockaddr*)&addr, sizeof(addr)) == -1) {
        std::cerr << "Failed to bind socket, errno: " << errno << std::endl;
        close(listen_fd_);
        return false;
    }

    // 监听
    if (listen(listen_fd_, SOMAXCONN) == -1) {
        std::cerr << "Failed to listen on socket, errno: " << errno << std::endl;
        close(listen_fd_);
        return false;
    }

    // 设置非阻塞
    if (!setNonBlocking(listen_fd_)) {
        close(listen_fd_);
        return false;
    }

    return true;
}

bool EpollServer::createEpoll() {
    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ == -1) {
        std::cerr << "Failed to create epoll instance, errno: " << errno << std::endl;
        return false;
    }

    epoll_event event;
    event.events = EPOLLIN; 
    event.data.fd = listen_fd_;

    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, listen_fd_, &event) == -1) {
        std::cerr << "Failed to add listen socket to epoll, errno: " << errno << std::endl;
        close(epoll_fd_);
        return false;
    }

    return true;
}

bool EpollServer::init()
{
  if (!createListenSocket())
  {
    return false;
  }
  if (!createEpoll())
  {
    return false;
  }
  is_running_ = true;
  return true;
}

void EpollServer::handleNewConnection(){
  while(true){
    sockaddr_in clientAddr{};
    socklen_t clientAddrLen = sizeof(clientAddr);
    int connfd = accept(listen_fd_, (sockaddr*)&clientAddr, &clientAddrLen);
    if(client_fd == -1){
      if(errno == EAGAIN || errno == EWOULDBLOCK){
        // 处理完所有连接
        break;
      }
      std::cerr << "accept() failed, errno: " << errno << std::endl;
      break;
    }

    // 设置非阻塞
    if(!setNonBlocking(client_fd)){
      close(client_fd);
      continue;
    }

    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = connfd;

    if(epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, connfd, &ev) == -1){
      std::cerr << "epoll_ctl(ADD) failed, client_fd: " << client_fd << ", errno: " << errno << std::endl;
      close(client_fd);
      continue;
    }

    std::cout << "New connection, fd=" << connfd << std::endl;
  }
}

void EpollServer::handleClient(int client_fd){
  char buffer[4096];
  while(true){
    ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer));
    if(bytes_read > 0){
      // 处理数据
      size_t sent = 0;
      while(sent < bytes_read){
        ssize_t bytes_sent = write(client_fd, buffer + sent, bytes_read - sent);
        if(bytes_sent > 0){
          sent += bytes_sent;
        } else if(bytes_sent == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)){
          // 写缓冲区满，稍后再写
          break;
        } else {
          std::cerr << "write() failed, fd: " << client_fd << ", errno: " << errno << std::endl;
          close(client_fd);
          return;
        }
      }
      std::cout << "Received data from fd " << client_fd << ": " << std::string(buffer, bytes_read) << std::endl;
    } else if(bytes_read == 0){
      // 客户端关闭连接
      std::cout << "Client disconnected, fd=" << client_fd << std::endl;
      close(client_fd);
      epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, client_fd, nullptr);
      return;
    }else{
      if(errno == EINTR){
        // 被信号中断，继续读取
        continue;
      }
      if(errno == EAGAIN || errno == EWOULDBLOCK){
        // 读完所有数据
        break;
      }
      std::cerr << "read() failed, fd: " << client_fd << ", errno: " << errno << std::endl;
      close(client_fd);
      epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, client_fd, nullptr);
      return;
    }
  }
}

void EpollServer::loop() {
    if(epoll_fd_ == -1 || listen_fd_ == -1){
      std::cerr << "Server not initialized properly." << std::endl;
      return;
    }

    const int MAX_EVENTS = 1024;
    epoll_event events[MAX_EVENTS];

    while (is_running_) {
        int n = epoll_wait(epoll_fd_, events, MAX_EVENTS, -1);
        if (n == -1) {
            if (errno == EINTR) {
                continue; // 被信号中断，继续等待
            }
            std::cerr << "epoll_wait() failed, errno: " << errno << std::endl;
            break;
        }

        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;
            uint32_t ev = events[i].events;

            if(fd == listen_fd_){
              // 处理新连接
              handleNewConnection();
            } else {
              if(ev & (EPOLLERR | EPOLLHUP)){
                // 出错或挂起，关闭连接
                std::cerr << "EPOLLERR or EPOLLHUP on fd: " << fd << std::endl;
                close(fd);
                epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
              } else if(ev & EPOLLIN){
                // 处理客户端数据
                handleClient(fd);
              }
            }
        }
    }
}