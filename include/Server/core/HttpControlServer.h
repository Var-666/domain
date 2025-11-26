#pragma once

#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <functional>
#include <memory>
#include <string>

class HttpControlServer {
  public:
    using tcp = boost::asio::ip::tcp;
    using readyCallback = std::function<bool()>;

    HttpControlServer(unsigned short port, readyCallback readyCheck);
    ~HttpControlServer();

    void start();
    void stop();

  private:
    void doAccept();
    boost::asio::awaitable<void> acceptLoop();
    boost::asio::awaitable<void> handleSession(std::shared_ptr<tcp::socket> sock);
    std::string handleRequest(const std::string& request);
    std::string buildResponse(int statusCode, const std::string& statusText, const std::string& body,
                              const std::string& contentType = "text/plain; charset=utf-8");

  private:
    boost::asio::io_context io_;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> workGuard_;

    tcp::acceptor acceptor_;
    readyCallback readyCheck_;
    std::thread thread_;
    std::atomic<bool> running_{false};
};
