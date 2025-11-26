#include "HttpControlServer.h"

#include <spdlog/spdlog.h>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <iostream>
#include <sstream>

#include "Metrics.h"
HttpControlServer::HttpControlServer(unsigned short port, readyCallback readyCheck)
    : io_(), workGuard_(boost::asio::make_work_guard(io_)), acceptor_(io_, tcp::endpoint(tcp::v4(), port)), readyCheck_(std::move(readyCheck)) {}

HttpControlServer::~HttpControlServer() { stop(); }

void HttpControlServer::start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        // 已经在运行了
        return;
    }

    doAccept();

    thread_ = std::thread([this]() {
        try {
            io_.run();
        } catch (const std::exception& ex) {
            SPDLOG_ERROR("[HttpControlServer] io_context exception:: {}", ex.what());
        }
    });
}

void HttpControlServer::stop() {
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false))
        return;

    // 停止 accept
    boost::system::error_code ec;
    acceptor_.close(ec);

    // 释放 work_guard，否则 io.run 不会退出
    workGuard_.reset();

    io_.stop();

    if (thread_.joinable())
        thread_.join();
}

void HttpControlServer::doAccept() { boost::asio::co_spawn(io_, acceptLoop(), boost::asio::detached); }

boost::asio::awaitable<void> HttpControlServer::acceptLoop() {
    using boost::asio::redirect_error;
    using boost::asio::use_awaitable;
    try {
        for (;;) {
            if (!running_) {
                co_return;
            }
            auto sock = std::make_shared<tcp::socket>(boost::asio::make_strand(io_));
            boost::system::error_code ec;
            co_await acceptor_.async_accept(*sock, redirect_error(use_awaitable, ec));
            if (!running_) {
                co_return;
            }
            if (ec) {
                if (ec != boost::asio::error::operation_aborted) {
                    SPDLOG_ERROR("[HttpControlServer] accept error: {}", ec.message());
                }
                continue;
            }
            boost::asio::co_spawn(sock->get_executor(), handleSession(sock), boost::asio::detached);
        }
    } catch (const std::exception& e) {
        SPDLOG_ERROR("[HttpControlServer] accept exception: {}", e.what());
    }
}

boost::asio::awaitable<void> HttpControlServer::handleSession(std::shared_ptr<tcp::socket> sock) {
    using boost::asio::redirect_error;
    using boost::asio::use_awaitable;
    boost::asio::streambuf buf;
    try {
        boost::system::error_code ec;
        std::size_t bytes = co_await boost::asio::async_read_until(*sock, buf, "\r\n\r\n", redirect_error(use_awaitable, ec));
        if (ec) {
            if (ec != boost::asio::error::operation_aborted) {
                SPDLOG_ERROR("[HttpControlServer] read error: {}", ec.message());
            }
            co_return;
        }

        std::istream is(&buf);
        std::string request;
        request.resize(bytes);
        is.read(&request[0], bytes);

        std::string response = handleRequest(request);

        ec = {};
        co_await boost::asio::async_write(*sock, boost::asio::buffer(response), redirect_error(use_awaitable, ec));
        if (ec && ec != boost::asio::error::operation_aborted) {
            SPDLOG_ERROR("[HttpControlServer] write error: {}", ec.message());
        }
    } catch (const std::exception& e) {
        SPDLOG_ERROR("[HttpControlServer] session exception: {}", e.what());
    }

    boost::system::error_code ignore;
    sock->shutdown(tcp::socket::shutdown_both, ignore);
    sock->close(ignore);
}

std::string HttpControlServer::handleRequest(const std::string& request) {
    std::istringstream iss(request);
    std::string method;
    std::string path;
    std::string version;
    iss >> method >> path >> version;

    if (method != "GET") {
        return buildResponse(405, "Method Not Allowed", "Only GET is supported\n");
    }

    if (path == "/metrics") {
        std::ostringstream oss;
        MetricsRegistry::Instance().printPrometheus(oss);
        return buildResponse(200, "OK", oss.str(), "text/plain; version=0.0.4; charset=utf-8");
    }

    if (path == "/healthz") {
        // 进程活着 → 200
        return buildResponse(200, "OK", "ok\n");
    }

    if (path == "/ready") {
        bool ready = true;
        if (readyCheck_) {
            ready = readyCheck_();
        }
        if (ready) {
            return buildResponse(200, "OK", "ready\n");
        } else {
            return buildResponse(503, "Service Unavailable", "not ready\n");
        }
    }

    // 其它路径：简单 404
    return buildResponse(404, "Not Found", "not found\n");
}

std::string HttpControlServer::buildResponse(int statusCode, const std::string& statusText, const std::string& body, const std::string& contentType) {
    std::ostringstream oss;
    oss << "HTTP/1.1 " << statusCode << " " << statusText << "\r\n";
    oss << "Content-Type: " << contentType << "\r\n";
    oss << "Content-Length: " << body.size() << "\r\n";
    oss << "Connection: close\r\n";
    oss << "\r\n";
    oss << body;
    return oss.str();
}
