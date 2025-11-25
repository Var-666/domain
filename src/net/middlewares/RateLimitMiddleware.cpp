#include "middlewares/RateLimitMiddleware.h"

#include <spdlog/spdlog.h>

#include "Codec.h"
#include "Metrics.h"

Middleware BuildRateLimitMiddleware(const Config& cfg, std::shared_ptr<MessageLimiter> limiter) {
    limiter->updateFromConfig(cfg);
    return [limiter](MessageContext& ctx, NextFunc next) {
        std::uint16_t t = ctx.msgType;
        if (!limiter->allow(t)) {
            MetricsRegistry::Instance().totalErrors().inc();
            MetricsRegistry::Instance().incMsgReject(t);
            SPDLOG_WARN("[RateLimit] msgType={} dropped conn={}", t, static_cast<const void*>(ctx.conn.get()));
            const auto& err = Config::Instance().errorFrames();
            LengthHeaderCodec::send(ctx.conn, err.msgRateLimitMsgType, err.msgRateLimitBody);
            return;
        }

        try {
            next(ctx);
        } catch (...) {
            limiter->onFinish(t);  // 防泄露
            throw;
        }
        limiter->onFinish(t);
    };
}
