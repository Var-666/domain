#include "Middlewares.h"

#include <spdlog/spdlog.h>
#include <random>

#include "Codec.h"
#include "MessageLimiter.h"
#include "Metrics.h"

namespace {
    bool SampleLog(double rate = 0.01) {
        if (rate <= 0.0)
            return false;
        if (rate >= 1.0)
            return true;
        thread_local std::mt19937 rng{std::random_device{}()};
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        return dist(rng) < rate;
    }
}  // namespace

void RegisterMiddlewares(MessageRouter& router, const Config& cfg) {
    // 1. 限流器：按 msgType 读取 Lua 配置
    auto limiter = std::make_shared<MessageLimiter>();
    limiter->updateFromConfig(cfg);

    // === 中间件 1：限流 + 统一耗时统计 ===
    router.use([limiter](MessageContext& ctx, NextFunc next) {
        std::uint16_t t = ctx.msgType;
        if (!limiter->allow(t)) {
            MetricsRegistry::Instance().totalErrors().inc();
            MetricsRegistry::Instance().incMsgReject(t);
            if (SampleLog()) {
                SPDLOG_WARN("[RateLimit] msgType={} dropped conn={}", t, static_cast<const void*>(ctx.conn.get()));
            }
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

    // 1.5 背压：拒绝低优先级消息（可配置），并回错误帧 + 计数
    const auto& bpCfg = cfg.backpressure();
    if (bpCfg.rejectLowPriority && !bpCfg.lowPriorityMsgTypes.empty()) {
        // 拷贝集合用于 lambda 捕获
        auto lowPri = bpCfg.lowPriorityMsgTypes;
        auto allow = bpCfg.alwaysAllowMsgTypes;
        auto errorType = bpCfg.errorMsgType;
        auto errorBody = bpCfg.errorBody;
        bool sendError = bpCfg.sendErrorFrame;

        router.use([lowPri, allow, errorType, errorBody, sendError](MessageContext& ctx, NextFunc next) {
            // 背压激活：当前有连接处于 read pause
            if (MetricsRegistry::Instance().backpressureActive().value() > 0) {
                if (!allow.empty() && allow.find(ctx.msgType) != allow.end()) {
                    next(ctx);
                    return;
                }
                if (lowPri.find(ctx.msgType) != lowPri.end()) {
                    MetricsRegistry::Instance().backpressureDroppedLowPri().inc();
                    MetricsRegistry::Instance().droppedFrames().inc();
                    MetricsRegistry::Instance().incMsgReject(ctx.msgType);
                    if (SampleLog()) {
                        SPDLOG_WARN("[Backpressure] drop low-priority msgType={} conn={}", ctx.msgType, static_cast<const void*>(ctx.conn.get()));
                    }
                    if (sendError && ctx.conn) {
                        LengthHeaderCodec::send(ctx.conn, errorType, errorBody);
                    }
                    return;
                }
            }
            next(ctx);
        });
    }

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
