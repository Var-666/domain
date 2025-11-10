#pragma once

#include <boost/asio.hpp>
#include <functional>
#include <memory>

#include "ThreadPool.h"

class AsioConnection;
using ConnectionPtr = std::shared_ptr<AsioConnection>;

class AsioConnection : public std::enable_shared_from_this<AsioConnection> {
  public:
    // 类型别名
    using tcp = boost::asio::ip::tcp;
    using MessageCallback = std::function<void(const ConnectionPtr&, const std::string&)>;
    using CloseCallback = std::function<void(const ConnectionPtr&)>;

    // 构造函数
    explicit AsioConnection(boost::asio::io_context& io_context, tcp::socket socket);
    void start();
    void send(const std::string& message);
    void close();

    void setMessageCallback(MessageCallback cb);
    void setCloseCallback(CloseCallback cb);

    tcp::socket& socket();

  private:
    void doRead();
    void doWrite();
    void handleClose();

  private:
    boost::asio::io_context& io_context_;
    boost::asio::ip::tcp::socket socket_;

    std::array<char, 1024> readBuf_;
    std::deque<std::string> writeQueue_;

    MessageCallback messageCallback_;
    CloseCallback closeCallback_;

    bool closing_{false};
};