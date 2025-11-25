#include "MessageRouter.h"

#include <spdlog/spdlog.h>

void MessageRouter::registerHandler(std::uint16_t msgType, MessageHandler handler) {
    std::lock_guard<std::mutex> lock(mtx_);
    HandlerEntry entry;
    entry.fmt = PayloadFormat::Raw;
    entry.rawHandler = std::move(handler);
    handlers_[msgType] = std::move(entry);
}

void MessageRouter::registerJson(std::uint16_t msgType, std::function<void(const ConnectionPtr&, const nlohmann::json&)> handler) {
    std::lock_guard<std::mutex> lock(mtx_);
    HandlerEntry entry;
    entry.fmt = PayloadFormat::Json;
    entry.jsonHandler = std::move(handler);
    handlers_[msgType] = std::move(entry);
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
    switch (handler.fmt) {
        case PayloadFormat::Raw:
            if (handler.rawHandler) {
                handler.rawHandler(ctx.conn, *ctx.body);
                return;
            }
            break;
        case PayloadFormat::Json:
            if (handler.jsonHandler) {
                try {
                    auto json = nlohmann::json::parse(*ctx.body);
                    handler.jsonHandler(ctx.conn, json);
                    return;
                } catch (const std::exception& ex) {
                    SPDLOG_WARN("Json parse failed for msgType={}, err={}", ctx.msgType, ex.what());
                    return;
                }
            }
            break;
        case PayloadFormat::Proto:
            if (handler.protoHandler && handler.protoFactory) {
                auto msg = handler.protoFactory();
                if (msg && msg->ParseFromString(*ctx.body)) {
                    handler.protoHandler(ctx.conn, *msg);
                    return;
                }
                SPDLOG_WARN("Proto parse failed for msgType={}", ctx.msgType);
                return;
            }
            break;
    }
    SPDLOG_WARN("No handler for msgType={}", ctx.msgType);
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
        entry.rawHandler = [def, msgType](const ConnectionPtr& conn, const std::string& body) { def(conn, msgType, body); };
    }
    return entry;
}
