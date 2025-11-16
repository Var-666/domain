#include "MessageRouter.h"

#include <spdlog/spdlog.h>

void MessageRouter::registerHandler(std::uint16_t msgType, MessageHandler handler) {
    std::lock_guard<std::mutex> lock(mtx_);
    handlers_[msgType] = std::move(handler);
}

void MessageRouter::setDefaultHandler(
    std::function<void(const ConnectionPtr&, std::uint16_t, const std::string&)> handler) {
    std::lock_guard<std::mutex> lock(mtx_);
    defaultHandler_ = std::move(handler);
}

void MessageRouter::use(Middleware mw) {
    std::lock_guard<std::mutex> lock(mtx_);
    middlewares_.push_back(std::move(mw));
}

void MessageRouter::onMessage(const ConnectionPtr& conn, std::uint16_t msgType, const std::string& body) {
    MessageContext ctx;
    ctx.conn = conn;
    ctx.msgType = msgType;
    ctx.body = &body;

    try {
        dispatch(0, ctx);
    } catch (const std::exception& ex) {
        SPDLOG_ERROR("MessageRouter::onMessage exception: {}", ex.what());
    } catch (...) {
        SPDLOG_ERROR("MessageRouter::onMessage unknown exception");
    }
}

void MessageRouter::dispatch(std::size_t idx, MessageContext& ctx) {
    // 如果还有 middleware，先执行 middleware[idx]
    if (idx < middlewares_.size()) {
        Middleware mw;
        {
            // 复制一份当前 middleware，避免持锁太久
            std::lock_guard<std::mutex> lock(mtx_);
            mw = middlewares_[idx];
        }
        if (mw) {
            NextFunc next = [this, idx](MessageContext& ctxRef) { this->dispatch(idx + 1, ctxRef); };
            mw(ctx, std::move(next));
            return;
        } else {
            // 当前 mw 为空，跳过
            dispatch(idx + 1, ctx);
            return;
        }
    }

    auto handler = getHandler(ctx.msgType);
    if (handler) {
        handler(ctx.conn, *ctx.body);
    } else {
        SPDLOG_WARN("No handler for msgType={}", ctx.msgType);
    }
}

MessageHandler MessageRouter::getHandler(std::uint16_t msgType) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = handlers_.find(msgType);
    if (it != handlers_.end()) {
        return it->second;
    }

    // 没有注册：返回一个 wrapper 调 defaultHandler_（如果有）
    if (defaultHandler_) {
        auto def = defaultHandler_;
        return [def, msgType](const ConnectionPtr& conn, const std::string& body) { def(conn, msgType, body); };
    }
    return {};
}
