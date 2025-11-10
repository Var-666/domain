#include "AsioSession.h"

#include <iostream>
#include <cerrno>
#include <memory>
#include <string>

AsioSession::AsioSession(boost::asio::io_context &io_context, tcp::socket socket, std::shared_ptr<ThreadPool> workerPool)
    : io_context_(io_context),
      socket_(std::move(socket)),
      worker_pool_(workerPool)
{
}

void AsioSession::start()
{
  std::cout << "Session started." << std::endl;
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
                              auto req = std::make_shared<std::string>(data_, length);
                              std::cout << "Received data: " << std::string(data_, length) << std::endl;
                              // 提交到工作线程池处理
                              worker_pool_->submit([this, self, req]()
                                                   {
                              // 模拟处理请求（这里直接回写收到的数据）
                              auto resp = req;
                              boost::asio::post(io_context_,
                                  [this, self, resp]()
                                  {
                                      // 回写响应
                                      do_write(resp->size());
                                  }); });
                            }
                            else
                            {
                              if (ec == boost::asio::error::eof)
                              {
                                // 连接关闭
                                std::cout << "Connection closed by peer." << std::endl;
                              }
                              else
                              {
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
                             }
                             else
                             {
                               std::cerr << "Write error: " << ec.message() << std::endl;
                             }
                           });
}