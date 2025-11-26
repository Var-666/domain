#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <ostream>
#include <string>
#include <unordered_map>

class Counter {
  public:
    Counter();
    void inc(std::int64_t n = 1);
    std::int64_t value() const;
    std::int64_t fetchAdd(std::int64_t n);

  private:
    std::atomic<std::int64_t> value_;
};

// 简单延迟统计：总次数 + 总耗时 + 分桶
class LatencyMetric {
  public:
    struct Snapshot {
        std::uint64_t count;
        double sumMs;
        std::uint64_t bucket[5];
    };

    LatencyMetric();
    // 观察一次耗时（毫秒）
    void observe(double ms);

    Snapshot snapshot() const;

    void print(const std::string& name, std::ostream& os) const;
    void printPrometheus(const std::string& name, std::ostream& os) const;

  private:
    static std::size_t bucketIndex(double ms);
    void aotmicAdd(std::atomic<double>& target, double value);

  private:
    std::atomic<std::uint64_t> count_;
    std::atomic<double> sumMs_;
    std::atomic<std::uint64_t> buckets_[5];
};

// 全局 Metrics 单例：后面要什么指标往里加就行
class MetricsRegistry {
  public:
    static MetricsRegistry& Instance();

    Counter& connections();                // 当前连接数（inc/dec）
    Counter& totalFrames();                // 总解包帧数
    Counter& totalErrors();                // 总错误数
    Counter& bytesIn();                    // 接收字节数
    Counter& bytesOut();                   // 发送字节数
    Counter& droppedFrames();              // 因限流/队列满被丢弃的帧数
    Counter& inflightFrames();             // 当前正在处理的帧数
    Counter& backpressureTriggered();      // 数值：触发次数
    Counter& backpressureActive();         // gauge：当前是否处于 backpressure（0/1）
    Counter& backpressureDroppedLowPri();  // 背压场景下丢弃低优先级消息
    Counter& backpressureDurationMs();     // 背压累计持续时长（ms）
    Counter& inflightRejects();            // 因 in-flight 超限被拒绝的次数
    Counter& tokenRejects();               // 令牌桶拒绝次数
    Counter& concurrentRejects();          // 并发超限拒绝次数
    Counter& sendQueueMaxBytes();          // 观察到的单连接发送队列峰值（bytes，Gauge）
    Counter& workerQueueSize();            // worker 队列长度（Gauge）
    Counter& workerLiveThreads();          // worker 线程活跃数量（Gauge）
    Counter& ipRejectConn();               // IP 连接拒绝计数
    Counter& ipRejectQps();                // IP QPS 拒绝计数
    void incIpRejectConn();
    void incIpRejectQps();

    void incMsgReject(std::uint16_t msgType);

    LatencyMetric& frameLatency();  // 每帧处理耗时（从 Codec 调用 handler 到返回）

    void onBackpressureEnter();
    void onBackpressureExit();

    void printSnapshot(std::ostream& os) const;
    void printPrometheus(std::ostream& os) const;

  private:
    MetricsRegistry() = default;

    Counter connections_;
    Counter totalFrames_;
    Counter totalErrors_;
    Counter bytesIn_;
    Counter bytesOut_;
    Counter droppedFrames_;
    Counter inflightFrames_;
    Counter backpressureTriggered_;
    Counter backpressureActive_;
    Counter backpressureDroppedLowPri_;
    Counter backpressureDurationMs_;
    Counter inflightRejects_;
    Counter workerQueueSize_;
    Counter workerLiveThreads_;
    Counter ipRejectConn_;
    Counter ipRejectQps_;
    Counter tokenRejects_;
    Counter concurrentRejects_;
    Counter sendQueueMaxBytes_;
    std::atomic<std::uint64_t> backpressureStartMs_{0};
    mutable std::mutex msgRejectsMtx_;
    std::unordered_map<std::uint16_t, std::atomic<std::uint64_t>> msgRejects_;

    LatencyMetric frameLatency_;
};
