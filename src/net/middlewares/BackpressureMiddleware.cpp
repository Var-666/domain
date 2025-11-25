#include "middlewares/BackpressureMiddleware.h"

#include <spdlog/spdlog.h>

#include "Codec.h"
#include "Metrics.h"

Middleware BuildBackpressureMiddleware(const Config& cfg) {
    const auto& bpCfg = cfg.backpressure();
    if (!(bpCfg.rejectLowPriority && !bpCfg.lowPriorityMsgTypes.empty())) {
        return {};
    }

    auto lowPri = bpCfg.lowPriorityMsgTypes;
    auto allow = bpCfg.alwaysAllowMsgTypes;
    bool sendError = bpCfg.sendErrorFrame;

    return [lowPri, allow, sendError](MessageContext& ctx, NextFunc next) {
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
                SPDLOG_WARN("[Backpressure] drop low-priority msgType={} conn={}", ctx.msgType, static_cast<const void*>(ctx.conn.get()));
                if (sendError && ctx.conn) {
                    const auto& err = Config::Instance().errorFrames();
                    LengthHeaderCodec::send(ctx.conn, err.backpressureMsgType, err.backpressureBody);
                }
                return;
            }
        }
        next(ctx);
    };
}
