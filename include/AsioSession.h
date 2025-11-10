#pragma once

#include <boost/asio.hpp>
#include <memory>

class AsioSession : public std::enable_shared_from_this<AsioSession>{
public:
  explicit AsioSession(boost::asio::ip::tcp::socket socket);
  void start();

private:
  void do_read();
  void do_write(std::size_t length);

  boost::asio::ip::tcp::socket socket_;
  char data_[1024];
};