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
            st->windowSec.store(nowSec(), std::memory_order_relaxed);
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

    if (cfg.maxConcurrent > 0) {
        int prev = st->concurrent.fetch_add(1, std::memory_order_relaxed);
        if (prev >= cfg.maxConcurrent) {
            st->concurrent.fetch_sub(1, std::memory_order_relaxed);
            st->dropped.fetch_add(1, std::memory_order_relaxed);
            SPDLOG_WARN("[MsgLimiter] msgType={} concurrent={} >= maxConcurrent={}", msgType, prev, cfg.maxConcurrent);
            return false;
        }
    }

    if (cfg.maxQps > 0) {
        std::uint64_t nowS = nowSec();
        std::uint64_t win = st->windowSec.load(std::memory_order_relaxed);
        if (nowS != win) {
            if (st->windowSec.compare_exchange_strong(win, nowS, std::memory_order_relaxed)) {
                st->qpsCount.store(0, std::memory_order_relaxed);
            }
        }
        int q = st->qpsCount.fetch_add(1, std::memory_order_relaxed) + 1;
        if (q > cfg.maxQps) {
            st->dropped.fetch_add(1, std::memory_order_relaxed);
            SPDLOG_WARN("[MsgLimiter] msgType={} qps={} > maxQps={}", msgType, q, cfg.maxQps);

            if (cfg.maxConcurrent > 0) {
                st->concurrent.fetch_sub(1, std::memory_order_relaxed);
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
    s.qps = static_cast<std::uint64_t>(st->qpsCount.load(std::memory_order_relaxed));
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
    st->windowSec.store(nowSec(), std::memory_order_relaxed);
    states_[msgType] = st;
    return st;
}

std::uint64_t MessageLimiter::nowSec() {
    using namespace std::chrono;
    auto now = system_clock::now().time_since_epoch();
    return duration_cast<seconds>(now).count();
}
