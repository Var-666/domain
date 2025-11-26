#include "MessageLimiter.h"

#include <spdlog/spdlog.h>

void MessageLimiter::updateFromConfig(const Config& cfg) {
    const auto& cfgMap = cfg.msgLimits();
    std::lock_guard<std::mutex> lock(mtx_);

    for (auto& kv : cfgMap) {
        auto msgType = kv.first;
        const auto& limitCfg = kv.second;

        auto it = states_.find(msgType);
        if (it == states_.end()) {
            auto st = std::make_shared<PerMsgState>();
            st->cfg = limitCfg;
            st->lastRefillNs = nowNs();
            st->tokens = limitCfg.maxQps;  // 初始充满桶
            states_[msgType] = std::move(st);
        } else {
            it->second->cfg = limitCfg;
        }
    }
}

bool MessageLimiter::allow(std::uint16_t msgType) {
    auto st = getOrCreateState(msgType);
    if (!st) {
        SPDLOG_ERROR("[MessageLimiter] getOrCreateState(msgType={}) returned null", msgType);
        return true;
    }
    const auto cfg = st->cfg;
    if (!cfg.enabled) {
        st->accepted.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    // 先检查 QPS
    if (cfg.maxQps > 0) {
        const double capacity = static_cast<double>(std::max(1, cfg.maxQps));
        const double ratePerNs = static_cast<double>(cfg.maxQps) / 1'000'000'000.0;

        std::lock_guard<std::mutex> lock(st->mtx);
        auto now = nowNs();

        // 计算流逝时间产生的令牌
        auto elapsed = static_cast<double>(now - st->lastRefillNs);
        if (elapsed > 0) {
            double refill = elapsed * ratePerNs;
            st->tokens = std::min(capacity, st->tokens + refill);
            st->lastRefillNs = now;
        }

        if (st->tokens < 1.0) {
            st->dropped.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        st->tokens -= 1.0;
    }

    // 再检查并发
    if (cfg.maxConcurrent > 0) {
        int prev = st->concurrent.fetch_add(1, std::memory_order_relaxed);
        if (prev >= cfg.maxConcurrent) {
            st->concurrent.fetch_sub(1, std::memory_order_relaxed);
            st->dropped.fetch_add(1, std::memory_order_relaxed);

            // 【重要】回滚刚才扣掉的令牌 (Revert Token)
            if (cfg.maxQps > 0) {
                std::lock_guard<std::mutex> lock(st->mtx);
                st->tokens += 1.0;
            }
            return false;
        }
    }
    st->accepted.fetch_add(1, std::memory_order_relaxed);
    return true;
}

void MessageLimiter::onFinish(std::uint16_t msgType) {
    auto st = getOrCreateState(msgType);
    if (!st)
        return;
    if (st->cfg.enabled && st->cfg.maxConcurrent > 0) {
        st->concurrent.fetch_sub(1, std::memory_order_relaxed);
    }
}

MessageLimiter::States MessageLimiter::getStats(std::uint16_t msgType) const {
    States s;
    auto st = getState(msgType);
    if (!st) {
        return s;
    }

    s.accepted = st->accepted.load(std::memory_order_relaxed);
    s.dropped = st->dropped.load(std::memory_order_relaxed);
    s.concurrent = static_cast<std::uint64_t>(st->concurrent.load(std::memory_order_relaxed));
    s.qps = 0;  // token bucket 不直接返回窗口 qps
    return s;
}

MessageLimiter::StatePtr MessageLimiter::getState(std::uint16_t msgType) const {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = states_.find(msgType);
    if (it != states_.end()) {
        return it->second;
    }
    return nullptr;
}

MessageLimiter::StatePtr MessageLimiter::getOrCreateState(std::uint16_t msgType) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = states_.find(msgType);
    if (it != states_.end()) {
        return it->second;
    }
    auto st = std::make_shared<PerMsgState>();
    st->cfg = MsgLimitConfig{};
    st->lastRefillNs = nowNs();
    st->tokens = 0;
    states_[msgType] = st;
    return st;
}

std::int64_t MessageLimiter::nowNs() {
    using namespace std::chrono;
    auto now = steady_clock::now().time_since_epoch();
    return duration_cast<nanoseconds>(now).count();
}
