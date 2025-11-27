#include "middlewares/RateLimitMiddleware.h"

#include <spdlog/spdlog.h>

#include "Codec.h"
#include "Metrics.h"

CoMiddleware BuildRateLimitMiddleware(const Config& cfg, std::shared_ptr<MessageLimiter> limiter) {
    limiter->updateFromConfig(cfg);
    return [limiter](std::shared_ptr<MessageContext> ctx, CoNextFunc next) -> boost::asio::awaitable<void> {
        std::uint16_t t = ctx->msgType;
        if (!limiter->allow(t)) {
            MetricsRegistry::Instance().totalErrors().inc();
            MetricsRegistry::Instance().incMsgReject(t);
            MetricsRegistry::Instance().setMsgRejectTrace(ctx->traceId, ctx->conn ? ctx->conn->sessionId() : "", t);
            MetricsRegistry::Instance().setTokenRejectTrace(ctx->traceId, ctx->conn ? ctx->conn->sessionId() : "");

            // 日志采样
            static thread_local uint64_t s_limitCount = 0;
            if (++s_limitCount % 10000 == 0) {
                SPDLOG_WARN("[RateLimit] Dropped (sampled): type={} trace={} sess={}", t, ctx->traceId, ctx->conn ? ctx->conn->sessionId() : "nil");
            }

            co_return;
        }

        //通过的才占用并发，处理完归还
        std::shared_ptr<void> concurrencyGuard(nullptr, [limiter, t](void*) { limiter->onFinish(t); });

        co_await next(ctx);
        co_return;
    };
}
