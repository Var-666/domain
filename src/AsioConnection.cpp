#include "AsioConnection.h"

#include <cerrno>
#include <iostream>
#include <memory>
#include <string>

AsioConnection::AsioConnection(boost::asio::io_context& io_context, tcp::socket socket)
    : io_context_(io_context), socket_(std::move(socket)) {}

void AsioConnection::start() { doRead(); }

void AsioConnection::send(const std::string& message) {
    auto self = shared_from_this();
    boost::asio::post(io_context_, [this, self, message] {
        bool writeInProgress = !writeQueue_.empty();
        writeQueue_.push_back(message);
        if (!writeInProgress) {
            doWrite();
        };
    });
}

void AsioConnection::close() {
    auto self = shared_from_this();
    boost::asio::post(io_context_, [this, self] { handleClose(); });
}

void AsioConnection::doRead() {
    auto self = shared_from_this();
    socket_.async_read_some(boost::asio::buffer(readBuf_),
                            [this, self](const boost::system::error_code& ec, std::size_t len) {
                                if (!ec) {
                                    if (len > 0) {
                                        std::string message(readBuf_.data(), len);
                                        if (messageCallback_) {
                                            messageCallback_(self, message);
                                        }
                                    }
                                    doRead();
                                } else {
                                    if (ec == boost::asio::error::eof || ec == boost::asio::error::connection_reset ||
                                        ec == boost::asio::error::operation_aborted) {
                                    } else {
                                        std::cerr << "Read error: " << ec.message() << std::endl;
                                    }
                                    handleClose();
                                }
                            });
}

void AsioConnection::doWrite() {
    auto self = shared_from_this();

    boost::asio::async_write(socket_, boost::asio::buffer(writeQueue_.front()),
                             [this, self](const boost::system::error_code& ec, std::size_t /*length*/) {
                                 if (!ec) {
                                     writeQueue_.pop_front();
                                     if (!writeQueue_.empty()) {
                                         doWrite();
                                     }
                                 } else {
                                     if (ec == boost::asio::error::operation_aborted) {
                                         std::cerr << "Write error: " << ec.message() << std::endl;
                                     }
                                     handleClose();
                                 }
                             });
}

void AsioConnection::handleClose() {
    if (closing_)
        return;
    closing_ = true;

    boost::system::error_code ec;
    socket_.shutdown(tcp::socket::shutdown_both, ec);
    socket_.close(ec);

    if (closeCallback_) {
        closeCallback_(shared_from_this());
    }
}

void AsioConnection::setMessageCallback(MessageCallback cb) { messageCallback_ = std::move(cb); }
void AsioConnection::setCloseCallback(CloseCallback cb) { closeCallback_ = std::move(cb); }
