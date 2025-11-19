#include "HttpControlServer.h"

#include <spdlog/spdlog.h>

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
            std::cerr << "[HttpControlServer] io_context exception: " << ex.what() << "\n";
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

    // ⭐ 释放 work_guard，否则 io.run 不会退出
    workGuard_.reset();

    io_.stop();

    if (thread_.joinable())
        thread_.join();
}

void HttpControlServer::doAccept() {
    if (!running_)
        return;

    auto sock = std::make_shared<tcp::socket>(io_);
    acceptor_.async_accept(*sock, [this, sock](const boost::system::error_code& ec) {
        if (!ec) {
            handleSession(sock);
        } else {
            if (ec != boost::asio::error::operation_aborted) {
                std::cerr << "[HttpControlServer] accept error: " << ec.message() << "\n";
            }
        }

        if (running_) {
            doAccept();
        }
    });
}

void HttpControlServer::handleSession(std::shared_ptr<tcp::socket> sock) {
    auto buf = std::make_shared<boost::asio::streambuf>();

    boost::asio::async_read_until(*sock, *buf, "\r\n\r\n", [this, sock, buf](const boost::system::error_code& ec, std::size_t bytes) {
        if (ec) {
            if (ec != boost::asio::error::operation_aborted) {
                std::cerr << "[HttpControlServer] read error: " << ec.message() << "\n";
            }
            return;
        }

        std::istream is(buf.get());
        std::string request;
        request.resize(bytes);
        is.read(&request[0], bytes);

        std::string response = handleRequest(request);

        auto respBuf = std::make_shared<std::string>(std::move(response));
        boost::asio::async_write(*sock, boost::asio::buffer(*respBuf), [sock, respBuf](const boost::system::error_code& ec2, std::size_t /*n*/) {
            boost::system::error_code ignore;
            sock->shutdown(tcp::socket::shutdown_both, ignore);
            sock->close(ignore);
            if (ec2 && ec2 != boost::asio::error::operation_aborted) {
                std::cerr << "[HttpControlServer] write error: " << ec2.message() << "\n";
            }
        });
    });
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
