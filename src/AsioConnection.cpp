#include "AsioConnection.h"

#include <spdlog/spdlog.h>

#include <cerrno>
#include <iostream>
#include <memory>
#include <string>

AsioConnection::AsioConnection(boost::asio::io_context& io_context, tcp::socket socket, size_t maxSendBufferBytes)
    : io_context_(io_context), socket_(std::move(socket)), maxSendBuf_(maxSendBufferBytes) {
    readBuf_ = BufferPool::Instance().acquire(4096);
}

void AsioConnection::start() {
    touch();
    doRead();
}

void AsioConnection::send(const std::string& message) { send(std::string_view(message)); }

void AsioConnection::send(std::string_view message) {
    if (closing_) {
        return;
    }
    // per-connection 发送缓存上限控制
    if (maxSendBuf_ > 0 && sendQueueBytes_ + message.size() > maxSendBuf_) {
        SPDLOG_ERROR("[AsioConnection] send buffer overflow, drop message, size= {}", message.size());
        return;
    }

    auto buf = BufferPool::Instance().acquire(message.size());
    if (!message.empty()) {
        buf->append(message.data(), message.size());
    }

    sendBuffer(buf);
}

void AsioConnection::sendBuffer(const BufferPool::Ptr& buf) {
    if (closing_) {
        return;
    }
    auto self = shared_from_this();

    // 所有写操作都回到 socket 所在的 executor，避免跨线程 data race
    boost::asio::post(socket_.get_executor(), [this, self, buf = std::move(buf)]() mutable {
        bool idle = sendQueue_.empty();
        sendQueueBytes_ += buf->readableBytes();
        sendQueue_.push_back(buf);
        if (idle) {
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
    socket_.async_read_some(
        boost::asio::buffer(readBuf_->beginWrite(), readBuf_->writableBytes()), [this, self](const boost::system::error_code& ec, std::size_t len) {
            if (!ec) {
                if (len > 0) {
                    touch();
                    readBuf_->hasWritten(len);

                    if (messageCallback_ && readBuf_->readableBytes() > 0) {
                        messageCallback_(self, *readBuf_);
                    }
                }
                doRead();
            } else {
                if (ec == boost::asio::error::eof || ec == boost::asio::error::connection_reset || ec == boost::asio::error::operation_aborted) {
                } else {
                    SPDLOG_ERROR("Write error: {}", ec.message());
                }
                handleClose();
            }
        });
}

void AsioConnection::doWrite() {
    if (sendQueue_.empty()) {
        writing_ = false;
        return;
    }

    writing_ = true;
    auto self = shared_from_this();

    // --- 合并策略参数：可以先写死，后面放到 Config 里 ---
    constexpr std::size_t kMaxBatchBytes = 16 * 1024;
    constexpr std::size_t kMaxBacthCount = 8;

    BufferPool::Ptr bufToSend;
    std::size_t bytesToSend = 0;

    if (sendQueue_.size() == 1) {
        // 只有一个，直接发，不合并，避免多余拷贝
        bufToSend = sendQueue_.front();  // 拷贝 shared_ptr 引用
        bytesToSend = bufToSend->readableBytes();
        sendQueue_.pop_front();
    } else {
        // 有多个，做小包合并
        bufToSend = BufferPool::Instance().acquire(kMaxBatchBytes);

        std::size_t batchCount = 0;
        while (!sendQueue_.empty() && batchCount < kMaxBacthCount) {
            BufferPool::Ptr& src = sendQueue_.front();
            std::size_t sz = src->readableBytes();

            // 限制总合并大小，至少合并一个
            if (batchCount > 0 && bytesToSend + sz > kMaxBacthCount) {
                break;
            }
            if (sz > 0) {
                bufToSend->append(src->peek(), sz);
                bytesToSend += sz;
            }

            sendQueue_.pop_front();
            ++batchCount;
        }
        if (bytesToSend == 0) {
            // 理论上不应该走到这里（表示队列里的 buffer readableBytes 都是 0）
            writing_ = false;
            return;
        }
    }
    auto buf = bufToSend;
    auto data = boost::asio::buffer(buf->peek(), bytesToSend);

    boost::asio::async_write(socket_, data, [this, self, buf, bytesToSend](const boost::system::error_code& ec, std::size_t len) {
        if (ec) {
            if (ec != boost::asio::error::operation_aborted) {
                SPDLOG_ERROR("Write error: {}", ec.message());
            }
            sendQueue_.clear();
            sendQueueBytes_ = 0;
            handleClose();
            return;
        }

        sendQueueBytes_ -= bytesToSend;

        if (!sendQueue_.empty()) {
            doWrite();
        } else {
            writing_ = false;
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

    sendQueue_.clear();
    sendQueueBytes_ = 0;

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
