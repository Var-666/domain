#include "middlewares/LoggingMiddleware.h"

#include <spdlog/spdlog.h>

CoMiddleware BuildLoggingMiddleware(const Config& cfg) {
    const auto& logCfg = cfg.log();
    bool debugLogEnabled = (logCfg.level == "debug" || logCfg.level == "trace");
    if (!debugLogEnabled) {
        return {};
    }
    return [](std::shared_ptr<MessageContext> ctx, CoNextFunc next) -> boost::asio::awaitable<void> {
        SPDLOG_DEBUG("recv msgType={} bodySize={}", ctx->msgType, ctx->body ? ctx->body->size() : 0);
        co_await next(ctx);
        co_return;
    };
}
