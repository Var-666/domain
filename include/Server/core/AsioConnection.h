#pragma once

#include <BufferPool.h>

#include <atomic>
#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <random>
#include <string_view>

#include "Buffer.h"
#include "IpLimiter.h"
#include "ThreadPool.h"

class AsioConnection;
using ConnectionPtr = std::shared_ptr<AsioConnection>;

/**
 * @brief 单 TCP 连接封装。
 * @details 负责异步读写、发送队列管理、小包合并与背压水位控制、最近活动时间记录。
 *          线程安全性：对外暴露的 send/touch 等可跨线程调用，内部通过 io_context 串行执行读写。
 */
class AsioConnection : public std::enable_shared_from_this<AsioConnection> {
  public:
    // 类型别名
    using tcp = boost::asio::ip::tcp;
    using MessageCallback = std::function<void(const ConnectionPtr&, Buffer&)>;
    using CloseCallback = std::function<void(const ConnectionPtr&)>;

    explicit AsioConnection(boost::asio::io_context& io_context, tcp::socket socket, size_t maxSendBufferBytes = 4 * 1024 * 1024);

    // 启动读写循环。
    void start();
    // 主动关闭连接。
    void close();

    // 发送字符串。
    void send(const std::string& message);
    // 发送字符串视图。
    void send(std::string_view message);
    // 发送已有 Buffer。
    void sendBuffer(const BufferPool::Ptr& buf);

    // 设置消息回调。
    void setMessageCallback(MessageCallback cb);
    // 设置关闭回调。
    void setCloseCallback(CloseCallback cb);

    tcp::socket& socket();

    // 更新最近活动时间。
    void touch();
    // 最近活动时间戳。
    std::uint64_t lastActiveMs() const;
    // 远端 IP（缓存）。
    std::string remoteIp() const;
    // 会话 ID（用于日志追踪）。
    std::string sessionId() const;
    // traceId（默认等于 sessionId，可被上游覆盖）。
    std::string traceId() const;
    // 是否处于背压暂停读
    bool isReadPaused() const;

  private:
    // 异步读循环（协程）。
    boost::asio::awaitable<void> readLoop();
    // 异步写循环（协程，含小包合并/背压）。
    boost::asio::awaitable<void> writeLoop();
    // 关闭处理。
    void handleClose();

  private:
    boost::asio::io_context& io_context_;  // I/O 上下文
    boost::asio::ip::tcp::socket socket_;  // 套接字
    boost::asio::steady_timer pauseTimer_; // 背压等待唤醒定时器

    BufferPool::Ptr readBuf_;  // 读缓冲
    std::atomic<bool> readPaused_{false};   // 背压暂停读标记

    std::size_t highWatermark_{0};  // 发送队列高水位（暂停读）
    std::size_t lowWatermark_{0};   // 发送队列低水位（恢复读）

    std::deque<BufferPool::Ptr> sendQueue_;  // 待发送队列
    std::size_t sendQueueBytes_{0};          // 待发送字节总量

    MessageCallback messageCallback_;  // 消息回调
    CloseCallback closeCallback_;      // 关闭回调

    bool closing_{false};   // 是否正在关闭
    bool writing_{false};   // 是否正在写
    size_t maxSendBuf_{0};  // 单连接发送缓冲上限

    std::atomic<std::uint64_t> lastActiveMs_{0};  // 最近活动时间
    std::string remoteIp_;                        // 缓存远端 IP
    std::string sessionId_;                       // 会话 ID
    std::string traceId_;                         // Trace ID（默认=sessionId）
};
