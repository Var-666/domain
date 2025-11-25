#pragma once


#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

#include "AsioConnection.h"
#include "Buffer.h"
#include "ConnectionManager.h"
#include "IdleConnectionManager.h"
#include "Metrics.h"
#include "ThreadPool.h"

/**
 * @brief 基于 Boost.Asio 的 TCP 服务端封装。
 * @details 负责监听、连接接入/移除、闲置连接回收、指标定时上报，以及 I/O 线程生命周期管理。
 *          线程安全性：公共 API（run/stop/stopAccept/closeAllConnections/set callbacks）可从外部线程调用，
 *          通过 io_context 保证事件串行；内部状态修改集中在 I/O 线程。
 */
class AsioServer {
  public:
    using tcp = boost::asio::ip::tcp;
    using MessageCallback = std::function<void(const ConnectionPtr&, Buffer&)>;
    using CloseCallback = std::function<void(const ConnectionPtr&)>;

    // 创建服务端，指定监听端口、I/O 线程数（0 表示自动）和空闲连接超时时间（毫秒）。
    explicit AsioServer(unsigned short port, std::size_t ioThreadsCount = 0, std::uint64_t idleTimeoutMs = 60000);

    // 启动 I/O 线程并运行事件循环。
    void run();
    // 停止接受与事件循环。
    void stop();

    // 仅停止接受新连接。
    void stopAccept();
    // 关闭所有已存在连接。
    void closeAllConnections();

    // 设置消息回调（由工作线程调用）。
    void setMessageCallback(MessageCallback cb);
    // 设置连接关闭回调。
    void setCloseCallback(CloseCallback cb);

    // 当前活跃连接数。
    std::size_t connectionCount() const;

    // 暴露 io_context 供外部组件使用。
    boost::asio::io_context& ioContext();

    // 是否还在接受新连接。
    bool isAccepting() const;

  private:
    // 异步接受新连接（协程入口）。
    void doAccept();
    boost::asio::awaitable<void> acceptLoop();
    // 定时输出指标。
    void scheduleMetricsReport();
    // 定时检查闲置连接。
    void scheduleIdleCheck();

  private:
    boost::asio::io_context io_context_;                // 主 I/O 上下文
    boost::asio::ip::tcp::acceptor acceptor_;           // 监听套接字

    std::size_t ioThreadsCount_{0};                     // I/O 线程数量
    std::vector<std::thread> ioThreads_;                // I/O 线程对象
    std::shared_ptr<ThreadPool> workerPool_;            // 业务工作线程池（预留）

    ConnectionManager connectionManager_;               // 连接管理器

    MessageCallback messageCallback_;                   // 消息回调
    CloseCallback closeCallback_;                       // 关闭回调

    boost::asio::steady_timer metricsTimer_;            // metrics 定时器

    IdleConnectionManager idleManager_;                 // 闲置连接检测
    boost::asio::steady_timer idleTimer_;               // 闲置检测定时器

    std::atomic<bool> accepting_{false};                // 是否仍在接受连接
};
