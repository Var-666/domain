#include "AsioServer.h"
#include "AsioSession.h"
#include <iostream>

AsioServer::AsioServer(unsigned short port)
    : acceptor_(io_context_, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port))
{
  std::cout << "Server started on port: " << port << std::endl;
}

void AsioServer::run()
{
  do_accept();
  io_context_.run();
}

void AsioServer::do_accept()
{
  acceptor_.async_accept(
      [this](boost::system::error_code ec, boost::asio::ip::tcp::socket socket)
      {
        if (!ec)
        {
          boost::system::error_code ec2;
          auto ep = socket.remote_endpoint(ec2);
          if (!ec2)
          {
            std::cout << "New connection from: "
                      << ep.address().to_string()
                      << ":" << ep.port()
                      << std::endl;
          }
          else
          {
            std::cerr << "remote_endpoint error: " << ec2.message() << std::endl;
          }
          // 处理新连接
          std::make_shared<AsioSession>(std::move(socket))->start();
        }
        else
        {
          std::cerr << "Accept error: " << ec.message() << std::endl;
        }
        do_accept();
      });
}