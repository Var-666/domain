#include "AsioConnection.h"

#include <spdlog/spdlog.h>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <cerrno>
#include <iostream>
#include <memory>
#include <string>

#include "Metrics.h"

AsioConnection::AsioConnection(boost::asio::io_context& io_context, tcp::socket socket, size_t maxSendBufferBytes)
    : io_context_(io_context), socket_(std::move(socket)), pauseTimer_(socket_.get_executor()), maxSendBuf_(maxSendBufferBytes) {
    highWatermark_ = maxSendBuf_ * 0.8;
    lowWatermark_ = maxSendBuf_ * 0.5;

    readBuf_ = BufferPool::Instance().acquire(4096);
    boost::system::error_code ec;
    auto ep = socket_.remote_endpoint(ec);
    if (!ec) {
        remoteIp_ = ep.address().to_string();
    }
}

void AsioConnection::start() {
    touch();
    boost::asio::co_spawn(socket_.get_executor(), readLoop(), boost::asio::detached);
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
    boost::asio::post(socket_.get_executor(), [this, self, buf]() mutable {
        bool idle = sendQueue_.empty();
        sendQueueBytes_ += buf->readableBytes();
        sendQueue_.push_back(buf);

        // ---------- Backpressure: 触发 ----------
        if (!readPaused_ && sendQueueBytes_ > highWatermark_) {
            readPaused_ = true;
            pauseTimer_.expires_at(std::chrono::steady_clock::time_point::max());
            MetricsRegistry::Instance().onBackpressureEnter();
            SPDLOG_WARN("[Backpressure] Pause read: queueBytes={} high={}", sendQueueBytes_, highWatermark_);
        }

        if (idle && !writing_) {
            writing_ = true;
            boost::asio::co_spawn(socket_.get_executor(), writeLoop(), boost::asio::detached);
        }
    });
}

void AsioConnection::close() {
    auto self = shared_from_this();
    boost::asio::post(io_context_, [this, self] { handleClose(); });
}

boost::asio::awaitable<void> AsioConnection::readLoop() {
    auto self = shared_from_this();
    try {
        for (;;) {
            if (closing_) {
                co_return;
            }
            if (readPaused_) {
                // 重置为“永不过期”，避免上次取消后的立即触发
                pauseTimer_.expires_at(std::chrono::steady_clock::time_point::max());
                boost::system::error_code ec;
                co_await pauseTimer_.async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, ec));
                if (ec == boost::asio::error::operation_aborted) {
                    // 被 cancel 唤醒，继续下一轮检查
                    continue;
                } else if (ec) {
                    SPDLOG_ERROR("pauseTimer error: {}", ec.message());
                    co_return;
                }
                // 正常超时（理论上不会发生）也继续下一轮
                continue;
            }

            readBuf_->ensureWritableBytes(4096);
            std::size_t len = co_await socket_.async_read_some(boost::asio::buffer(readBuf_->beginWrite(), readBuf_->writableBytes()), boost::asio::use_awaitable);

            if (len > 0) {
                MetricsRegistry::Instance().bytesIn().inc(len);
                touch();
                readBuf_->hasWritten(len);

                if (messageCallback_ && readBuf_->readableBytes() > 0) {
                    messageCallback_(self, *readBuf_);
                }
            }
        }
    } catch (const boost::system::system_error& e) {
        auto ec = e.code();
        if (ec == boost::asio::error::eof || ec == boost::asio::error::connection_reset || ec == boost::asio::error::operation_aborted) {
        } else {
            SPDLOG_ERROR("Read error: {}", ec.message());
        }
        handleClose();
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Read exception: {}", e.what());
        handleClose();
    }
}

boost::asio::awaitable<void> AsioConnection::writeLoop() {
    auto self = shared_from_this();
    try {
        // --- 合并策略参数：可以先写死，后面放到 Config 里 ---
        constexpr std::size_t kMaxBatchCount = 16;

        std::vector<boost::asio::const_buffer> sendingBuffers;
        std::vector<BufferPool::Ptr> inFlightBufs;

        // 预分配内存，避免 push_back 时扩容
        sendingBuffers.reserve(kMaxBatchCount);
        inFlightBufs.reserve(kMaxBatchCount);

        while (!sendQueue_.empty()) {
            sendingBuffers.clear();
            inFlightBufs.clear();

            std::size_t bytesToSend = 0;

            // 合并数据
            while (!sendQueue_.empty() && inFlightBufs.size() < kMaxBatchCount) {
                auto& buf = sendQueue_.front();
                if (buf->readableBytes() > 0) {
                    sendingBuffers.emplace_back(buf->peek(), buf->readableBytes());
                    inFlightBufs.push_back(buf);
                    bytesToSend += buf->readableBytes();
                }
                sendQueue_.pop_front();
            }

            if (bytesToSend == 0) {
                if (sendQueue_.empty())
                    break;
                continue;
            }

            // 发送数据
            co_await boost::asio::async_write(socket_, sendingBuffers, boost::asio::use_awaitable);

            // 统计和背压
            sendQueueBytes_ -= bytesToSend;
            MetricsRegistry::Instance().bytesOut().inc(bytesToSend);

            if (readPaused_ && sendQueueBytes_ <= lowWatermark_) {
                if (!closing_) {
                    readPaused_ = false;
                    pauseTimer_.cancel();
                    MetricsRegistry::Instance().onBackpressureExit();
                }
            }
        }
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Write exception: {}", e.what());
        handleClose();
    }
    writing_ = false;
}

void AsioConnection::handleClose() {
    if (closing_)
        return;
    closing_ = true;

    if (readPaused_) {
        readPaused_ = false;
        pauseTimer_.cancel_one();
        MetricsRegistry::Instance().onBackpressureExit();
    }

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
std::string AsioConnection::remoteIp() const { return remoteIp_; }

void AsioConnection::touch() {
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    lastActiveMs_.store(static_cast<std::uint64_t>(ms), std::memory_order_relaxed);
}

std::uint64_t AsioConnection::lastActiveMs() const { return lastActiveMs_.load(std::memory_order_relaxed); }
