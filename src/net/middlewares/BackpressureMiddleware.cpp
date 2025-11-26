#include "middlewares/BackpressureMiddleware.h"

#include <spdlog/spdlog.h>

#include "Codec.h"
#include "Metrics.h"

CoMiddleware BuildBackpressureMiddleware(const Config& cfg) {
    const auto& bpCfg = cfg.backpressure();
    if (!(bpCfg.rejectLowPriority && !bpCfg.lowPriorityMsgTypes.empty())) {
        return {};
    }

    auto lowPri = bpCfg.lowPriorityMsgTypes;
    auto allow = bpCfg.alwaysAllowMsgTypes;
    constexpr std::size_t kGlobalBpThreshold = 100;  // 全局背压触发低优先级拒绝的最小触发连接数

    return [lowPri = std::move(lowPri), allow = std::move(allow), kGlobalBpThreshold](std::shared_ptr<MessageContext> ctx, CoNextFunc next) -> boost::asio::awaitable<void> {
        // 获取全局以及连接背压情况
        bool isSelfCongested = ctx->conn && ctx->conn->isReadPaused();
        bool isGlobalPanic = false;

        if (!isSelfCongested) {
            auto globalBp = MetricsRegistry::Instance().backpressureActive().value();
            isGlobalPanic = (globalBp > kGlobalBpThreshold);
        }

        if (isSelfCongested || isGlobalPanic) {
            // 白名单检查
            if (!allow.empty() && allow.find(ctx->msgType) != allow.end()) {
                co_await next(ctx);
                co_return;
            }
            if (lowPri.find(ctx->msgType) != lowPri.end()) {
                MetricsRegistry::Instance().backpressureDroppedLowPri().inc();
                MetricsRegistry::Instance().droppedFrames().inc();
                MetricsRegistry::Instance().incMsgReject(ctx->msgType);

                // 日志采样
                static thread_local uint64_t s_dropCount = 0;
                if (++s_dropCount % 1000 == 0) {
                    SPDLOG_WARN("[Backpressure] Dropping low-pri (sampled): type={} selfPaused={} globalPanic={}", ctx->msgType, isSelfCongested, isGlobalPanic);
                }
                co_return;
            }
        }
        co_await next(ctx);
        co_return;
    };
}
