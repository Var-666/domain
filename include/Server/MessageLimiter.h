#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "Config.h"

// 按 msgType 做限流：
//   - maxConcurrent：同时在处理的请求数
//   - maxQps：简单的“每秒最多请求数”，滑窗粒度为 1 秒
// 内部自己维护 per-msg 的计数 & 简单统计。
class MessageLimiter {
  public:
    MessageLimiter() = default;

    // 从 Config 加载一份 msgType 的限流策略
    void updateFromConfig(const Config& cfg);

    // 判断是否允许这个 msgType 通过（不做并发计数自增）
    bool allow(std::uint16_t msgType);

    // 请求执行结束（handler 返回后调用）
    void onFinish(std::uint16_t msgType);

    struct States {
        std::uint64_t accepted = 0;
        std::uint64_t dropped = 0;
        std::uint64_t concurrent = 0;
        std::uint64_t qps = 0;
    };

    States getStats(std::uint16_t msgType) const;

  private:
    struct PerMsgState {
        MsgLimitConfig cfg;
        std::atomic<int> concurrent{0};

        std::atomic<std::uint64_t> windowSec{0};
        std::atomic<int> qpsCount{0};

        std::atomic<std::uint64_t> accepted{0};
        std::atomic<std::uint64_t> dropped{0};
    };

    using StatePtr = std::shared_ptr<PerMsgState>;

    StatePtr getState(std::uint16_t msgType) const;
    StatePtr getOrCreateState(std::uint16_t msgType);

    static std::uint64_t nowSec();

  private:
    mutable std::mutex mtx_;
    std::unordered_map<std::uint16_t, StatePtr> states_;
};