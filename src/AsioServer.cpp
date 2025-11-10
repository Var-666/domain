#include "AsioServer.h"
#include "AsioSession.h"
#include "ThreadPool.h"
#include <iostream>

AsioServer::AsioServer(unsigned short port, size_t ioThreadsCount, size_t workerThreadsCount)
    : acceptor_(io_context_, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port)),
      io_Threads_(ioThreadsCount)
{
  if (io_Threads_ <= 0){
    io_Threads_ = std::thread::hardware_concurrency();
  }
  if (workerThreadsCount <= 0){
    workerThreadsCount = std::thread::hardware_concurrency();
  }
  // 创建工作线程池
  worker_pool_ = std::make_shared<ThreadPool>(workerThreadsCount);

  std::cout << "AsioServer listen on 0.0.0.0:" << port
            << " | io_threads=" << io_Threads_
            << " | worker_threads=" << workerThreadsCount
            << std::endl;
}

void AsioServer::run()
{
  // 启动线程池处理I/O事件
  for (size_t i = 0; i < io_Threads_; ++i)
  {
    threads_.emplace_back([this](){
      try{
        io_context_.run();
      }catch(const std::exception &e){
        std::cerr << "IO thread exception: " << e.what() << std::endl;
      } });
  }
  // 开始接受连接
  do_accept();

  // 等待所有线程完成
  for (std::thread &t : threads_)
  {
    if (t.joinable())
      t.join();
  }

  // 关闭工作线程池
  if (worker_pool_)
  {
    worker_pool_->shutdown();
  }
}

void AsioServer::stop()
{
  io_context_.stop();
  if (worker_pool_)
  {
    worker_pool_->shutdown();
  }
}

void AsioServer::do_accept()
{
  acceptor_.async_accept(
      [this](boost::system::error_code ec, boost::asio::ip::tcp::socket socket)
      {
        if (!ec){
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
          std::make_shared<AsioSession>(io_context_, std::move(socket), worker_pool_)->start();
        }
        else
        {
          std::cerr << "Accept error: " << ec.message() << std::endl;
        }
        do_accept();
      });
}