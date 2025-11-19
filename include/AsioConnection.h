#pragma once

#include <BufferPool.h>

#include <atomic>
#include <boost/asio.hpp>
#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <string_view>

#include "Buffer.h"
#include "ThreadPool.h"

class AsioConnection;
using ConnectionPtr = std::shared_ptr<AsioConnection>;

class AsioConnection : public std::enable_shared_from_this<AsioConnection> {
  public:
    // 类型别名
    using tcp = boost::asio::ip::tcp;
    using MessageCallback = std::function<void(const ConnectionPtr&, Buffer&)>;
    using CloseCallback = std::function<void(const ConnectionPtr&)>;

    // 构造函数
    explicit AsioConnection(boost::asio::io_context& io_context, tcp::socket socket, size_t maxSendBufferBytes = 4 * 1024 * 1024);
    void start();
    void close();

    void send(const std::string& message);
    void send(std::string_view message);
    void sendBuffer(const BufferPool::Ptr& buf);

    void setMessageCallback(MessageCallback cb);
    void setCloseCallback(CloseCallback cb);

    tcp::socket& socket();

    void touch();

    std::uint64_t lastActiveMs() const;

  private:
    void doRead();
    void doWrite();
    void handleClose();

  private:
    boost::asio::io_context& io_context_;
    boost::asio::ip::tcp::socket socket_;

    BufferPool::Ptr readBuf_;

    std::deque<BufferPool::Ptr> sendQueue_;
    std::size_t sendQueueBytes_{0};

    MessageCallback messageCallback_;
    CloseCallback closeCallback_;

    bool closing_{false};
    bool writing_{false};
    size_t maxSendBuf_{0};

    std::atomic<std::uint64_t> lastActiveMs_{0};
};