#include "AsioConnection.h"

#include <cerrno>
#include <iostream>
#include <memory>
#include <string>

AsioConnection::AsioConnection(boost::asio::io_context& io_context, tcp::socket socket, size_t maxSendBufferBytes)
    : io_context_(io_context), socket_(std::move(socket)), maxSendBuf_(maxSendBufferBytes) {
    readBuf_ = BufferPool::Instance().acquire(4096);
    writeBuf_ = BufferPool::Instance().acquire(4096);
}

void AsioConnection::start() {
    touch();
    doRead();
}

void AsioConnection::send(const std::string& message) {
    auto self = shared_from_this();
    bool ok = true;

    boost::asio::post(io_context_, [this, self, message, &ok] {
        if (closing_) {
            ok = false;
            return;
        }
        // 背压：超过上限则触发策略（本例：直接关闭）
        if (writeBuf_->readableBytes() + message.size() > maxSendBuf_) {
            std::cerr << "Backpressure: send buffer exceeded. Closing connection.\n";
            ok = false;
            handleClose();
            return;
        }
        writeBuf_->append(message);
        if (!writing_) {
            writing_ = true;
            doWrite();
        }
    });
}

void AsioConnection::close() {
    auto self = shared_from_this();
    boost::asio::post(io_context_, [this, self] { handleClose(); });
}

void AsioConnection::doRead() {
    auto self = shared_from_this();

    readBuf_->ensureWritableBytes(4096);
    socket_.async_read_some(boost::asio::buffer(readBuf_->beginWrite(), readBuf_->writableBytes()),
                            [this, self](const boost::system::error_code& ec, std::size_t len) {
                                if (!ec) {
                                    if (len > 0) {
                                        touch();
                                        readBuf_->hasWritten(len);

                                        if (messageCallback_ && readBuf_->readableBytes() > 0) {
                                            std::string chunk = readBuf_->retrieveAllAsString();
                                            messageCallback_(self, chunk);
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

    // 直接把 Buffer 的可读区交给 async_write
    auto bytes = writeBuf_->readableBytes();
    if (bytes == 0) {
        writing_ = false;
        return;
    }

    boost::asio::async_write(socket_, boost::asio::buffer(writeBuf_->peek(), bytes),
                             [this, self](const boost::system::error_code& ec, std::size_t len) {
                                 if (!ec) {
                                     writeBuf_->retrieve(len);
                                     if (writeBuf_->readableBytes() > 0) {
                                         doWrite();
                                     } else {
                                         writing_ = false;
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

    readBuf_->retrieveAll();
    writeBuf_->retrieveAll();

    if (closeCallback_) {
        closeCallback_(shared_from_this());
    }
}

void AsioConnection::setMessageCallback(MessageCallback cb) { messageCallback_ = std::move(cb); }
void AsioConnection::setCloseCallback(CloseCallback cb) { closeCallback_ = std::move(cb); }

boost::asio::ip::tcp::socket& AsioConnection::socket() { return socket_; }

void AsioConnection::touch() {
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    lastActiveMs_.store(static_cast<std::uint64_t>(ms), std::memory_order_relaxed);
}

std::uint64_t AsioConnection::lastActiveMs() const { return lastActiveMs_.load(std::memory_order_relaxed); }
