#pragma once

#include <cstdint>

class EpollServer{
  private:
    int listen_fd_;
    int epoll_fd_;
    uint16_t port_;
    bool is_running_;
  public:
    EpollServer(uint16_t port);
    ~EpollServer();

    bool init();
    void loop();
    void stop();

  private:
    bool setNonBlocking(int fd);
    bool createListenSocket();
    bool createEpoll();

    void handleNewConnection();
    void handleClient(int client_fd);
};
