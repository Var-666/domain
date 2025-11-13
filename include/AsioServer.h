#pragma once

#include <boost/asio.hpp>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

#include "AsioConnection.h"
#include "ConnectionManager.h"
#include "IdleConnectionManager.h"
#include "Metrics.h"
#include "ThreadPool.h"

class AsioServer {
  public:
    using tcp = boost::asio::ip::tcp;
    using MessageCallback = std::function<void(const ConnectionPtr&, const std::string&)>;
    using CloseCallback = std::function<void(const ConnectionPtr&)>;

    explicit AsioServer(unsigned short port, std::size_t ioThreadsCount = 0, std::size_t workerThreadsCount = 0, std::uint64_t idleTimeoutMs = 60000);

    void run();
    void stop();

    void setMessageCallback(MessageCallback cb);
    void setCloseCallback(CloseCallback cb);

    std::size_t connectionCount() const;

  private:
    void doAccept();
    void scheduleMetricsReport();
    void scheduleIdleCheck();

  private:
    boost::asio::io_context io_context_;
    boost::asio::ip::tcp::acceptor acceptor_;

    std::size_t ioThreadsCount_{0};
    std::vector<std::thread> ioThreads_;
    std::shared_ptr<ThreadPool> workerPool_;

    ConnectionManager connectionManager_;

    MessageCallback messageCallback_;
    CloseCallback closeCallback_;

    boost::asio::steady_timer metricsTimer_;  // metrics 定时器

    IdleConnectionManager idleManager_;
    boost::asio::steady_timer idleTimer_;
};