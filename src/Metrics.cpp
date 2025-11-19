#include "Metrics.h"

Counter::Counter() : value_(0) {}

void Counter::inc(int64_t n) { value_.fetch_add(n, std::memory_order_relaxed); }

std::int64_t Counter::value() const { return value_.load(std::memory_order_relaxed); }

void LatencyMetric::aotmicAdd(std::atomic<double>& target, double value) {
    double old = target.load(std::memory_order_relaxed);
    double desired;
    do {
        desired = old + value;
    } while (!target.compare_exchange_weak(old, desired, std::memory_order_relaxed, std::memory_order_relaxed));
}

LatencyMetric::LatencyMetric() : count_(0), sumMs_(0) {
    for (auto& b : buckets_) {
        b.store(0, std::memory_order_relaxed);
    }
}

void LatencyMetric::observe(double ms) {
    count_.fetch_add(1, std::memory_order_relaxed);
    aotmicAdd(sumMs_, ms);

    std::size_t idx = bucketIndex(ms);
    buckets_[idx].fetch_add(1, std::memory_order_relaxed);
}

LatencyMetric::Snapshot LatencyMetric::snapshot() const {
    Snapshot s;
    s.count = count_.load(std::memory_order_relaxed);
    s.sumMs = sumMs_.load(std::memory_order_relaxed);
    for (std::size_t i = 0; i < 5; ++i) {
        s.bucket[i] = buckets_[i].load(std::memory_order_relaxed);
    }
    return s;
}

void LatencyMetric::print(const std::string& name, std::ostream& os) const {
    auto s = snapshot();
    os << name << ": count=" << s.count;
    if (s.count > 0) {
        double avg = s.sumMs / s.count;
        os << ", avg=" << std::fixed << std::setprecision(3) << avg << "ms";
    }
    os << " | buckets(ms) [0-1):" << s.bucket[0] << " [1-5):" << s.bucket[1] << " [5-10):" << s.bucket[2] << " [10-50):" << s.bucket[3]
       << " [50+):" << s.bucket[4] << "\n";
}

void LatencyMetric::printPrometheus(const std::string& name, std::ostream& os) const {
    auto s = snapshot();

    const double bounds[5] = {1.0, 5.0, 20.0, 100.0, std::numeric_limits<double>::infinity()};
    std::uint64_t cum = 0;

    os << "# TYPE " << name << " histogram\n";

    for (std::size_t i = 0; i < 5; ++i) {
        cum += s.bucket[i];
        os << name << "_bucket{le=\"";
        if (i < 4) {
            os << bounds[i];
        } else {
            os << "+Inf";
        }
        os << "\"} " << cum << "\n";
    }

    os << name << "_sum " << std::fixed << std::setprecision(6) << s.sumMs << "\n";
    os << name << "_count " << s.count << "\n";
}

std::size_t LatencyMetric::bucketIndex(double ms) {
    if (ms < 1.0)
        return 0;
    if (ms < 5.0)
        return 1;
    if (ms < 10.0)
        return 2;
    if (ms < 50.0)
        return 3;
    return 4;
}

MetricsRegistry& MetricsRegistry::Instance() {
    static MetricsRegistry instance;
    return instance;
}

Counter& MetricsRegistry::connections() { return connections_; }

Counter& MetricsRegistry::totalFrames() { return totalFrames_; }

Counter& MetricsRegistry::totalErrors() { return totalErrors_; }

Counter& MetricsRegistry::bytesIn() { return bytesIn_; }

Counter& MetricsRegistry::bytesOut() { return bytesOut_; }

Counter& MetricsRegistry::droppedFrames() { return droppedFrames_; }

Counter& MetricsRegistry::inflightFrames() { return inflightFrames_; }

LatencyMetric& MetricsRegistry::frameLatency() { return frameLatency_; }

void MetricsRegistry::printSnapshot(std::ostream& os) const {
    os << "======================================= Metrics Snapshot ===========================================\n";
    os << "connections     = " << connections_.value() << "\n";
    os << "totalFrames     = " << totalFrames_.value() << "\n";
    os << "totalErrors     = " << totalErrors_.value() << "\n";
    os << "bytesIn         = " << bytesIn_.value() << "\n";
    os << "bytesOut        = " << bytesOut_.value() << "\n";
    os << "droppedFrames   = " << droppedFrames_.value() << "\n";
    frameLatency_.print("frameLatency", os);
    os << "====================================================================================================\n";
}

void MetricsRegistry::printPrometheus(std::ostream& os) const {
    // Counters / Gauges
    os << "# TYPE server_connections gauge\n";
    os << "server_connections " << connections_.value() << "\n\n";

    os << "# TYPE server_total_frames counter\n";
    os << "server_total_frames " << totalFrames_.value() << "\n\n";

    os << "# TYPE server_total_errors counter\n";
    os << "server_total_errors " << totalErrors_.value() << "\n\n";

    os << "# TYPE server_bytes_in counter\n";
    os << "server_bytes_in " << bytesIn_.value() << "\n\n";

    os << "# TYPE server_bytes_out counter\n";
    os << "server_bytes_out " << bytesOut_.value() << "\n\n";

    os << "# TYPE server_dropped_frames counter\n";
    os << "server_dropped_frames " << droppedFrames_.value() << "\n\n";

    os << "# TYPE server_inflight_frames gauge\n";
    os << "server_inflight_frames " << inflightFrames_.value() << "\n\n";

    // Histograms
    frameLatency_.printPrometheus("server_frame_latency_ms", os);
    os << "\n";
}
