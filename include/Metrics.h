#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <ostream>
#include <string>

class Counter {
  public:
    Counter();
    void inc(std::int64_t n = 1);
    std::int64_t value() const;

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

    Counter& connections();    // 当前连接数（inc/dec）
    Counter& totalFrames();    // 总解包帧数
    Counter& totalErrors();    // 总错误数
    Counter& bytesIn();        // 接收字节数
    Counter& bytesOut();       // 发送字节数
    Counter& droppedFrames();  // 因限流/队列满被丢弃的帧数
    Counter& inflightFrames();  // 当前正在处理的帧数（可以当做 gauge 用

    LatencyMetric& frameLatency();  // 每帧处理耗时（从 Codec 调用 handler 到返回）

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

    LatencyMetric frameLatency_;
};