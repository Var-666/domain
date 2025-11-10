#include "AsioSession.h"

#include <iostream>
#include <cerrno>

AsioSession::AsioSession(boost::asio::ip::tcp::socket socket)
    : socket_(std::move(socket))
{
}

void AsioSession::start()
{
    do_read();
}

void AsioSession::do_read()
{
    auto self(shared_from_this());
    socket_.async_read_some(boost::asio::buffer(data_, sizeof(data_)),
        [this, self](boost::system::error_code ec, std::size_t length)
        {
            if (!ec)
            {
                std::cout << "Received data: " << std::string(data_, length) << std::endl;
                do_write(length);
            }else{
              if(ec == boost::asio::error::eof){
                  // 连接关闭
                  std::cout << "Connection closed by peer." << std::endl;
              }else{  
                  std::cerr << "Read error: " << ec.message() << std::endl;
              }
            }
        });
}

void AsioSession::do_write(std::size_t length)
{
    auto self(shared_from_this());
    boost::asio::async_write(socket_, boost::asio::buffer(data_, length),
        [this, self](boost::system::error_code ec, std::size_t /*length*/)
        {
            if (!ec)
            {
                do_read();
            }else{
              std::cerr << "Write error: " << ec.message() << std::endl;
            }
        });
}