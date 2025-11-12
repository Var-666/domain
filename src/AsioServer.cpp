#include "AsioServer.h"

#include <iostream>

#include "AsioConnection.h"
#include "ThreadPool.h"

AsioServer::AsioServer(unsigned short port, size_t ioThreadsCount, size_t workerThreadsCount)
    : acceptor_(io_context_, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port)),
      ioThreadsCount_(ioThreadsCount) {
    if (ioThreadsCount_ <= 0) {
        ioThreadsCount_ = std::thread::hardware_concurrency();
    }
    if (workerThreadsCount <= 0) {
        workerThreadsCount = std::thread::hardware_concurrency();
    }
    // 创建工作线程池
    workerPool_ = std::make_shared<ThreadPool>(workerThreadsCount);

    std::cout << "AsioServer listen on 0.0.0.0:" << port << " | io_threads=" << ioThreadsCount_
              << " | worker_threads=" << workerThreadsCount << std::endl;

    // 开始接受连接
    doAccept();
}

void AsioServer::run() {
    // 启动线程池处理I/O事件
    for (size_t i = 0; i < ioThreadsCount_; ++i) {
        ioThreads_.emplace_back([this]() {
            try {
                io_context_.run();
            } catch (const std::exception& e) {
                std::cerr << "IO thread exception: " << e.what() << std::endl;
            }
        });
    }

    // 等待所有线程完成
    for (std::thread& t : ioThreads_) {
        if (t.joinable())
            t.join();
    }

    // 关闭工作线程池
    if (workerPool_) {
        workerPool_->shutdown();
    }
}

void AsioServer::stop() {
    io_context_.stop();
    if (workerPool_) {
        workerPool_->shutdown();
    }
}

void AsioServer::doAccept() {
    acceptor_.async_accept([this](boost::system::error_code ec, boost::asio::ip::tcp::socket socket) {
        if (!ec) {
            auto connection = std::make_shared<AsioConnection>(io_context_, std::move(socket));
            connectionManager_.add(connection);

            // 设置连接的回调
            connection->setMessageCallback([this](const ConnectionPtr& conn, const std::string& message) {
                if (!messageCallback_) {
                    return;
                }
                // 将消息处理任务提交给工作线程池
                auto weak = std::weak_ptr<AsioConnection>(conn);
                workerPool_->submit([this, weak, message]() {
                    if (auto shared = weak.lock()) {
                        messageCallback_(shared, message);
                    }
                });
            });

            // 设置关闭回调
            connection->setCloseCallback([this](const ConnectionPtr& conn) {
                connectionManager_.remove(conn);
                if (closeCallback_) {
                    closeCallback_(conn);
                }
            });

            connection->start();
        } else {
            std::cerr << "Accept error: " << ec.message() << std::endl;
        }
        doAccept();
    });
}

void AsioServer::setMessageCallback(MessageCallback cb) { messageCallback_ = std::move(cb); }

void AsioServer::setCloseCallback(CloseCallback cb) { closeCallback_ = std::move(cb); }

std::size_t AsioServer::connectionCount() const { return connectionManager_.size(); }