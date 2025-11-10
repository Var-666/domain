#pragma once

#include <boost/asio.hpp>

class AsioServer
{
public:
  explicit AsioServer(unsigned short port);

  void run();

private:
  void do_accept();

  boost::asio::io_context io_context_;
  boost::asio::ip::tcp::acceptor acceptor_;
};