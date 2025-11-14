#include "AsioServer.h"

#include <spdlog/spdlog.h>

#include <iostream>

#include "AsioConnection.h"
#include "ThreadPool.h"

namespace {
    std::atomic<int> gInflight{0};
    constexpr int kMaxInflight = 10000;
}  // namespace

AsioServer::AsioServer(unsigned short port, size_t ioThreadsCount, size_t workerThreadsCount,
                       std::uint64_t idleTimeoutMs)
    : acceptor_(io_context_, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port)),
      ioThreadsCount_(ioThreadsCount),
      metricsTimer_(io_context_),
      idleManager_(IdleConnectionManager::Duration(idleTimeoutMs)),
      idleTimer_(io_context_) {
    if (ioThreadsCount_ <= 0) {
        ioThreadsCount_ = std::thread::hardware_concurrency();
    }
    if (workerThreadsCount <= 0) {
        workerThreadsCount = std::thread::hardware_concurrency();
    }
    // 创建工作线程池
    workerPool_ = std::make_shared<ThreadPool>(workerThreadsCount, 10000);

    // 开始接受连接
    doAccept();

    scheduleIdleCheck();
}

void AsioServer::run() {
    // 启动线程池处理I/O事件
    for (size_t i = 0; i < ioThreadsCount_; ++i) {
        ioThreads_.emplace_back([this]() {
            try {
                io_context_.run();
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
            idleManager_.add(connection);

            // connection 计数 +1
            MetricsRegistry::Instance().connections().inc();

            // 设置连接的回调
            connection->setMessageCallback([this](const ConnectionPtr& conn, const std::string& message) {
                if (!messageCallback_) {
                    return;
                }
                // 全局 in - flight 限制
                int cur = gInflight.fetch_add(1, std::memory_order_relaxed);
                if (cur >= kMaxInflight) {
                    gInflight.fetch_sub(1, std::memory_order_relaxed);
                    MetricsRegistry::Instance().totalErrors().inc();
                    SPDLOG_ERROR("too many in-flight requests, drop message");
                    return;
                }

                // 将消息处理任务提交给工作线程池
                auto weak = std::weak_ptr<AsioConnection>(conn);
                try {
                    workerPool_->submit([this, weak, message]() {
                        if (auto shared = weak.lock()) {
                            try {
                                messageCallback_(shared, message);
                            } catch (const std::exception& ex) {
                                SPDLOG_ERROR("userMessageCallback exception:{} ", ex.what());
                            } catch (...) {
                                SPDLOG_ERROR("userMessageCallback unknown exception");
                            }
                        }
                        gInflight.fetch_sub(1, std::memory_order_relaxed);
                    });
                } catch (const std::exception& ex) {
                    gInflight.fetch_sub(1, std::memory_order_relaxed);
                    MetricsRegistry::Instance().totalErrors().inc();
                    SPDLOG_ERROR("ThreadPool submit failed: {}", ex.what());
                }
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