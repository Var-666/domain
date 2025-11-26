#include "MessageRouter.h"

#include <spdlog/spdlog.h>

#include <boost/asio/detached.hpp>

void MessageRouter::registerHandler(std::uint16_t msgType, CoMessageHandler handler) {
    std::lock_guard<std::mutex> lock(mtx_);
    HandlerEntry entry;
    entry.fmt = PayloadFormat::Raw;
    entry.rawHandler = std::move(handler);
    handlers_[msgType] = std::move(entry);
}

void MessageRouter::registerJson(std::uint16_t msgType, std::function<boost::asio::awaitable<void>(const ConnectionPtr&, const nlohmann::json&)> handler) {
    std::lock_guard<std::mutex> lock(mtx_);
    HandlerEntry entry;
    entry.fmt = PayloadFormat::Json;
    entry.jsonHandler = std::move(handler);
    handlers_[msgType] = std::move(entry);
}

void MessageRouter::setDefaultHandler(std::function<boost::asio::awaitable<void>(const ConnectionPtr&, std::uint16_t, const std::string&)> handler) {
    std::lock_guard<std::mutex> lock(mtx_);
    defaultHandler_ = std::move(handler);
}

void MessageRouter::use(CoMiddleware mw) {
    std::lock_guard<std::mutex> lock(mtx_);
    middlewares_.push_back(std::move(mw));
}

void MessageRouter::onMessage(const ConnectionPtr& conn, std::uint16_t msgType, const std::string& body) {
    auto ctx = std::make_shared<MessageContext>();
    ctx->conn = conn;
    ctx->msgType = msgType;
    ctx->body = std::make_shared<std::string>(body);

    try {
        auto exec = conn->socket().get_executor();
        boost::asio::co_spawn(exec, dispatch(0, ctx), boost::asio::detached);
    } catch (const std::exception& ex) {
        SPDLOG_ERROR("MessageRouter::onMessage exception: {}", ex.what());
    } catch (...) {
        SPDLOG_ERROR("MessageRouter::onMessage unknown exception");
    }
}

boost::asio::awaitable<void> MessageRouter::dispatch(std::size_t idx, std::shared_ptr<MessageContext> ctx) {
    // 如果还有 middleware，先执行 middleware[idx]
    if (idx < middlewares_.size()) {
        CoMiddleware mw = middlewares_[idx];
        if (mw) {
            CoNextFunc next = [this, idx](std::shared_ptr<MessageContext> ctxRef) -> boost::asio::awaitable<void> { co_await this->dispatch(idx + 1, std::move(ctxRef)); };
            co_await mw(ctx, std::move(next));
        } else {
            // 当前 mw 为空，跳过
            co_await dispatch(idx + 1, std::move(ctx));
        }
        co_return;
    }

    auto handler = getHandler(ctx->msgType);
    switch (handler.fmt) {
        case PayloadFormat::Raw:
            if (handler.rawHandler) {
                co_await handler.rawHandler(ctx->conn, *ctx->body);
            }
            break;
        case PayloadFormat::Json:
            if (handler.jsonHandler) {
                try {
                    auto json = nlohmann::json::parse(*ctx->body);
                    co_await handler.jsonHandler(ctx->conn, json);
                } catch (const std::exception& ex) {
                    SPDLOG_WARN("Json parse failed for msgType={}, err={}", ctx->msgType, ex.what());
                }
            }
            break;
        case PayloadFormat::Proto:
            if (handler.protoHandler && handler.protoFactory) {
                auto msg = handler.protoFactory();
                if (msg && msg->ParseFromString(*ctx->body)) {
                    co_await handler.protoHandler(ctx->conn, *msg);
                } else {
                    SPDLOG_WARN("Proto parse failed for msgType={}", ctx->msgType);
                }
            }
            break;
        default:
            SPDLOG_WARN("No handler for msgType={}", ctx->msgType);
            break;
    }
    co_return;
}

HandlerEntry MessageRouter::getHandler(std::uint16_t msgType) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = handlers_.find(msgType);
    if (it != handlers_.end()) {
        return it->second;
    }

    // 没有注册：返回一个 wrapper 调 defaultHandler_（如果有）
    HandlerEntry entry;
    if (defaultHandler_) {
        entry.fmt = PayloadFormat::Raw;
        auto def = defaultHandler_;
        entry.rawHandler = [def, msgType](const ConnectionPtr& conn, std::string_view body) -> boost::asio::awaitable<void> { co_await def(conn, msgType, std::string(body)); };
    }
    return entry;
}
