#include "middlewares/Middlewares.h"

#include <spdlog/spdlog.h>

#include "middlewares/BackpressureMiddleware.h"
#include "middlewares/LoggingMiddleware.h"
#include "middlewares/RateLimitMiddleware.h"

void RegisterMiddlewares(MessageRouter& router, const Config& cfg) {
    auto limiter = std::make_shared<MessageLimiter>();

    // === 中间件 ：限流 ===
    auto rateLimitMw = BuildRateLimitMiddleware(cfg, limiter);
    if (rateLimitMw) {
        router.use(rateLimitMw);
    }

    // === 中间件 ：背压 ===
    auto bpMw = BuildBackpressureMiddleware(cfg);
    if (bpMw) {
        router.use(bpMw);
    }

    // === 中间件 ：简单日志 ===
    auto logMw = BuildLoggingMiddleware(cfg);
    if (logMw) {
        router.use(logMw);
    }

    // === 中间件 ：预留一个“鉴权/会话校验”位（现在可以是空壳） ===
    router.use([](std::shared_ptr<MessageContext> ctx, CoNextFunc next) -> boost::asio::awaitable<void> {
        // TODO: 将来在这里做：
        //  - 解析 token / session
        //  - 从 ctx.conn 的某个 userData 里拿用户信息
        //  - 没权限就 return，别调 next
        co_await next(ctx);
        co_return;
    });
}
