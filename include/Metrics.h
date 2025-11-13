#pragma once

#include <atomic>
#include <chrono>
#include <iomanip>
#include <iostream>
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

    void aotmicAdd(std::atomic<double>& target, double value);

    LatencyMetric();
    void observe(double ms);

    Snapshot snapshot() const;

    void print(const std::string& name, std::ostream& os) const;

  private:
    static std::size_t bucketIndex(double ms);

  private:
    std::atomic<std::uint64_t> count_;
    std::atomic<double> sumMs_;
    std::atomic<std::uint64_t> buckets_[5];
};

// 全局 Metrics 单例：后面要什么指标往里加就行
class MetricsRegistry {
  public:
    static MetricsRegistry& Instance();

    Counter& connections();
    Counter& totalFrames();
    Counter& totalErrors();
    LatencyMetric& frameLatency();

    void printSnapshot(std::ostream& os) const;

  private:
    MetricsRegistry() = default;

    Counter connections_;
    Counter totalFrames_;
    Counter totalErrors_;
    LatencyMetric frameLatency_;
};