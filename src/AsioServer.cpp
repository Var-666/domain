#include "AsioServer.h"

#include <spdlog/spdlog.h>

#include <iostream>

#include "AsioConnection.h"
#include "Config.h"
#include "ThreadPool.h"

AsioServer::AsioServer(unsigned short port, size_t ioThreadsCount, std::uint64_t idleTimeoutMs)
    : acceptor_(io_context_, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port)),
      ioThreadsCount_(ioThreadsCount),
      metricsTimer_(io_context_),
      idleManager_(IdleConnectionManager::Duration(idleTimeoutMs)),
      idleTimer_(io_context_) {
    if (ioThreadsCount_ <= 0) {
        ioThreadsCount_ = std::thread::hardware_concurrency();
    }
    // 开始接受连接
    doAccept();

    scheduleIdleCheck();
}

void AsioServer::run() {
    // 启动线程池处理I/O事件
    for (size_t i = 0; i < ioThreadsCount_; ++i) {
        ioThreads_.emplace_back([this]() {
            try {
                accepting_.store(true, std::memory_order_relaxed);
                io_context_.run();
                accepting_.store(false, std::memory_order_relaxed);
            } catch (const std::exception& e) {
                SPDLOG_ERROR("IO thread exception: {}", e.what());
            }
        });
    }

    // 等待所有线程完成
    for (std::thread& t : ioThreads_) {
        if (t.joinable())
            t.join();
    }
}

void AsioServer::stop() {
    stopAccept();
    io_context_.stop();
}

void AsioServer::stopAccept() {
    accepting_.store(false, std::memory_order_relaxed);
    boost::system::error_code ec;
    acceptor_.cancel(ec);
    acceptor_.close(ec);
    if (ec) {
        SPDLOG_WARN("stopAccept error: {}", ec.message());
    } else {
        SPDLOG_INFO("stopAccept: new connections will no longer be accepted");
    }
}

void AsioServer::closeAllConnections() {
    SPDLOG_INFO("closeAllConnections: closing {} connections", connectionManager_.size());
    connectionManager_.forEach([](const ConnectionPtr& c) { c->close(); });
}

void AsioServer::doAccept() {
    if (!acceptor_.is_open()) {
        return;
    }

    acceptor_.async_accept([this](boost::system::error_code ec, boost::asio::ip::tcp::socket socket) {
        if (!ec) {
            auto connection = std::make_shared<AsioConnection>(io_context_, std::move(socket));

            connectionManager_.add(connection);
            idleManager_.add(connection);

            // connection 计数 +1
            MetricsRegistry::Instance().connections().inc();

            // 设置连接的回调
            connection->setMessageCallback([this](const ConnectionPtr& conn, Buffer& buf) {
                if (!messageCallback_) {
                    return;
                }
                messageCallback_(conn, buf);
            });

            // 设置关闭回调
            connection->setCloseCallback([this](const ConnectionPtr& conn) {
                connectionManager_.remove(conn);
                idleManager_.remove(conn);
                // MetricsRegistry::Instance().connections().inc(-1);
                if (closeCallback_) {
                    closeCallback_(conn);
                }
                scheduleMetricsReport();
            });

            connection->start();
        } else {
            if (ec == boost::asio::error::operation_aborted || !acceptor_.is_open()) {
                return;  // 停止接受时触发，直接退出
            }
            SPDLOG_ERROR("Accept error: {}", ec.message());
        }
        doAccept();
    });
}

void AsioServer::scheduleMetricsReport() {
    using namespace std::chrono_literals;
    metricsTimer_.expires_after(5s);
    metricsTimer_.async_wait([this](const boost::system::error_code& ec) {
        if (!ec) {
            MetricsRegistry::Instance().printSnapshot(std::cout);
        } else {
            if (ec != boost::asio::error::operation_aborted) {
                SPDLOG_ERROR("metrics_timer error:{}", ec.message());
            }
        }
    });
}

void AsioServer::scheduleIdleCheck() {
    using namespace std::chrono_literals;
    idleTimer_.expires_after(10s);
    idleTimer_.async_wait([this](const boost::system::error_code& ec) {
        if (!ec) {
            idleManager_.check();
            scheduleIdleCheck();
        } else {
            if (ec != boost::asio::error::operation_aborted) {
                SPDLOG_ERROR("idle_timer error: {}", ec.message());
            }
        }
    });
}

void AsioServer::setMessageCallback(MessageCallback cb) { messageCallback_ = std::move(cb); }

void AsioServer::setCloseCallback(CloseCallback cb) { closeCallback_ = std::move(cb); }

std::size_t AsioServer::connectionCount() const { return connectionManager_.size(); }

boost::asio::io_context& AsioServer::ioContext() { return io_context_; }

bool AsioServer::isAccepting() const { return accepting_.load(std::memory_order_relaxed); }
