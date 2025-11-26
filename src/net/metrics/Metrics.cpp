#include "Metrics.h"

Counter::Counter() : value_(0) {}

void Counter::inc(int64_t n) { value_.fetch_add(n, std::memory_order_relaxed); }

std::int64_t Counter::value() const { return value_.load(std::memory_order_relaxed); }

std::int64_t Counter::fetchAdd(std::int64_t n) { return value_.fetch_add(n, std::memory_order_relaxed); }

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

Counter& MetricsRegistry::backpressureTriggered() { return backpressureTriggered_; }

Counter& MetricsRegistry::backpressureActive() { return backpressureActive_; }

Counter& MetricsRegistry::backpressureDroppedLowPri() { return backpressureDroppedLowPri_; }

Counter& MetricsRegistry::backpressureDurationMs() { return backpressureDurationMs_; }

Counter& MetricsRegistry::inflightRejects() { return inflightRejects_; }

Counter& MetricsRegistry::tokenRejects() { return tokenRejects_; }

Counter& MetricsRegistry::concurrentRejects() { return concurrentRejects_; }

Counter& MetricsRegistry::sendQueueMaxBytes() { return sendQueueMaxBytes_; }

Counter& MetricsRegistry::workerQueueSize() { return workerQueueSize_; }

Counter& MetricsRegistry::workerLiveThreads() { return workerLiveThreads_; }

Counter& MetricsRegistry::ipRejectConn() { return ipRejectConn_; }

Counter& MetricsRegistry::ipRejectQps() { return ipRejectQps_; }

void MetricsRegistry::incMsgReject(std::uint16_t msgType) {
    std::lock_guard<std::mutex> lock(msgRejectsMtx_);
    auto& c = msgRejects_[msgType];
    c.fetch_add(1, std::memory_order_relaxed);
}

void MetricsRegistry::incIpRejectConn() {
    ipRejectConn_.inc();
    totalErrors_.inc();
}

void MetricsRegistry::incIpRejectQps() {
    ipRejectQps_.inc();
    totalErrors_.inc();
}

LatencyMetric& MetricsRegistry::frameLatency() { return frameLatency_; }

namespace {
    std::uint64_t nowMs() {
        auto now = std::chrono::steady_clock::now().time_since_epoch();
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
    }
}  // namespace

void MetricsRegistry::onBackpressureEnter() {
    backpressureTriggered_.inc();
    auto prev = backpressureActive_.fetchAdd(1);
    if (prev == 0) {
        backpressureStartMs_.store(nowMs(), std::memory_order_relaxed);
    }
}

void MetricsRegistry::onBackpressureExit() {
    auto prev = backpressureActive_.fetchAdd(-1);
    if (prev <= 0) {
        backpressureActive_.fetchAdd(1);
        return;
    }
    if (prev == 1) {
        auto start = backpressureStartMs_.exchange(0, std::memory_order_relaxed);
        if (start > 0) {
            auto dur = nowMs() - start;
            backpressureDurationMs_.inc(static_cast<std::int64_t>(dur));
        }
    }
}

void MetricsRegistry::printSnapshot(std::ostream& os) const {
    os << "======================================= Metrics Snapshot ===========================================\n";
    os << "connections     = " << connections_.value() << "\n";
    os << "totalFrames     = " << totalFrames_.value() << "\n";
    os << "totalErrors     = " << totalErrors_.value() << "\n";
    os << "bytesIn         = " << bytesIn_.value() << "\n";
    os << "bytesOut        = " << bytesOut_.value() << "\n";
    os << "droppedFrames   = " << droppedFrames_.value() << "\n";
    os << "backpressureTriggered   = " << backpressureTriggered_.value() << "\n";
    os << "backpressureActive   = " << backpressureActive_.value() << "\n";
    os << "backpressureDropLowPri   = " << backpressureDroppedLowPri_.value() << "\n";
    os << "backpressureDurationMs   = " << backpressureDurationMs_.value() << "\n";
    os << "inflightRejects   = " << inflightRejects_.value() << "\n";
    os << "tokenRejects   = " << tokenRejects_.value() << "\n";
    os << "concurrentRejects   = " << concurrentRejects_.value() << "\n";
    os << "sendQueueMaxBytes   = " << sendQueueMaxBytes_.value() << "\n";
    os << "workerQueueSize   = " << workerQueueSize_.value() << "\n";
    os << "workerLiveThreads   = " << workerLiveThreads_.value() << "\n";
    os << "ipRejectConn   = " << ipRejectConn_.value() << "\n";
    os << "ipRejectQps    = " << ipRejectQps_.value() << "\n";
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

    os << "# TYPE server_backpressure_triggered_total counter\n";
    os << "server_backpressure_triggered_total " << backpressureTriggered_.value() << "\n\n";

    os << "# TYPE server_backpressure_active gauge\n";
    os << "server_backpressure_active " << backpressureActive_.value() << "\n\n";

    os << "# TYPE server_backpressure_drop_lowpri counter\n";
    os << "server_backpressure_drop_lowpri " << backpressureDroppedLowPri_.value() << "\n\n";

    os << "# TYPE server_backpressure_duration_ms counter\n";
    os << "server_backpressure_duration_ms " << backpressureDurationMs_.value() << "\n\n";

    os << "# TYPE server_inflight_rejects_total counter\n";
    os << "server_inflight_rejects_total " << inflightRejects_.value() << "\n\n";

    os << "# TYPE server_token_rejects_total counter\n";
    os << "server_token_rejects_total " << tokenRejects_.value() << "\n\n";

    os << "# TYPE server_concurrent_rejects_total counter\n";
    os << "server_concurrent_rejects_total " << concurrentRejects_.value() << "\n\n";

    os << "# TYPE server_send_queue_max_bytes gauge\n";
    os << "server_send_queue_max_bytes " << sendQueueMaxBytes_.value() << "\n\n";

    os << "# TYPE server_worker_queue_size gauge\n";
    os << "server_worker_queue_size " << workerQueueSize_.value() << "\n\n";

    os << "# TYPE server_worker_live_threads gauge\n";
    os << "server_worker_live_threads " << workerLiveThreads_.value() << "\n\n";

    os << "# TYPE server_ip_reject_conn_total counter\n";
    os << "server_ip_reject_conn_total " << ipRejectConn_.value() << "\n\n";

    os << "# TYPE server_ip_reject_qps_total counter\n";
    os << "server_ip_reject_qps_total " << ipRejectQps_.value() << "\n\n";

    os << "# TYPE server_inflight_frames gauge\n";
    os << "server_inflight_frames " << inflightFrames_.value() << "\n\n";

    if (!msgRejects_.empty()) {
        os << "# TYPE server_msg_reject_total counter\n";
        std::lock_guard<std::mutex> lock(msgRejectsMtx_);
        for (const auto& kv : msgRejects_) {
            os << "server_msg_reject_total{msgType=\"" << kv.first << "\"} " << kv.second.load(std::memory_order_relaxed) << "\n";
        }
        os << "\n";
    }

    // Histograms
    frameLatency_.printPrometheus("server_frame_latency_ms", os);
    os << "\n";
}
