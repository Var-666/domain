#include "middlewares/LoggingMiddleware.h"

#include <spdlog/spdlog.h>

Middleware BuildLoggingMiddleware(const Config& cfg) {
    const auto& logCfg = cfg.log();
    bool debugLogEnabled = (logCfg.level == "debug" || logCfg.level == "trace");
    if (!debugLogEnabled) {
        return {};
    }
    return [](MessageContext& ctx, NextFunc next) {
        SPDLOG_DEBUG("recv msgType={} bodySize={}", ctx.msgType, ctx.body ? ctx.body->size() : 0);
        next(ctx);
    };
}
