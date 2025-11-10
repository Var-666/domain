#pragma once

#include "ThreadPool.h"

#include <boost/asio.hpp>
#include <vector>
#include <thread>
class AsioServer
{
public:
  explicit AsioServer(unsigned short port, size_t ioThreadsCount = 0, size_t workerThreadsCount = 0);

  void run();

  void stop();

private:
  void do_accept();

  boost::asio::io_context io_context_;
  boost::asio::ip::tcp::acceptor acceptor_;

  size_t io_Threads_{0};
  std::vector<std::thread> threads_;

  std::shared_ptr<ThreadPool> worker_pool_;
};