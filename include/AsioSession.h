#pragma once

#include "ThreadPool.h"

#include <boost/asio.hpp>
#include <memory>

class AsioSession : public std::enable_shared_from_this<AsioSession>
{
public:
  using tcp = boost::asio::ip::tcp;
  explicit AsioSession(boost::asio::io_context &io_context, tcp::socket socket, std::shared_ptr<ThreadPool> workerPool);
  void start();

private:
  void do_read();
  void do_write(std::size_t length);

private:
  boost::asio::io_context &io_context_;
  boost::asio::ip::tcp::socket socket_;
  std::shared_ptr<ThreadPool> worker_pool_;
  char data_[1024];
};