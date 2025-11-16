#include "Middlewares.h"

#include <spdlog/spdlog.h>

#include "MessageLimiter.h"
#include "Metrics.h"

void RegisterMiddlewares(MessageRouter& router, const Config& cfg) {
    // 1. 限流器：按 msgType 读取 Lua 配置
    auto limiter = std::make_shared<MessageLimiter>();
    limiter->updateFromConfig(cfg);

    // === 中间件 1：限流 + 统一耗时统计 ===
    router.use([limiter](MessageContext& ctx, NextFunc next) {
        std::uint16_t t = ctx.msgType;
        if (!limiter->allow(t)) {
            MetricsRegistry::Instance().totalErrors().inc();
            SPDLOG_WARN("[RateLimit] msgType={} dropped", t);
            // TODO: 这里也可以回一个错误帧给客户端（需要协议约定）
            return;
        }

        try {
            next(ctx);
        } catch (...) {
            limiter->onFinish(t);  // 防泄露
            throw;
        }
        limiter->onFinish(t);
    });

    // === 中间件 2：简单日志（可按配置开启/关闭） ===
    const auto& logCfg = cfg.log();
    bool debugLogEnabled = (logCfg.level == "debug" || logCfg.level == "trace");
    if (debugLogEnabled) {
        router.use([](MessageContext& ctx, NextFunc next) {
            SPDLOG_DEBUG("recv msgType={} bodySize={}", ctx.msgType, ctx.body ? ctx.body->size() : 0);
            next(ctx);
        });
    }

    // === 中间件 3：预留一个“鉴权/会话校验”位（现在可以是空壳） ===
    router.use([](MessageContext& ctx, NextFunc next) {
        // TODO: 将来在这里做：
        //  - 解析 token / session
        //  - 从 ctx.conn 的某个 userData 里拿用户信息
        //  - 没权限就 return，别调 next
        next(ctx);
    });
}