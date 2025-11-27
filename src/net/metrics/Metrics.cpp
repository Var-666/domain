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
    os << " | buckets(ms) [0-1):" << s.bucket[0] << " [1-5):" << s.bucket[1] << " [5-10):" << s.bucket[2] << " [10-50):" << s.bucket[3] << " [50+):" << s.bucket[4] << "\n";
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

void MetricsRegistry::setTokenRejectTrace(const std::string& traceId, const std::string& sessionId) {
    std::lock_guard<std::mutex> lock(exemplarMtx_);
    lastTokenRejectTrace_ = traceId;
    lastTokenRejectSession_ = sessionId;
    lastTokenRejectValue_ = tokenRejects_.value();
}

void MetricsRegistry::setConcurrentRejectTrace(const std::string& traceId, const std::string& sessionId) {
    std::lock_guard<std::mutex> lock(exemplarMtx_);
    lastConcurrentRejectTrace_ = traceId;
    lastConcurrentRejectSession_ = sessionId;
    lastConcurrentRejectValue_ = concurrentRejects_.value();
}

void MetricsRegistry::setBackpressureDropTrace(const std::string& traceId, const std::string& sessionId) {
    std::lock_guard<std::mutex> lock(exemplarMtx_);
    lastBackpressureTrace_ = traceId;
    lastBackpressureSession_ = sessionId;
    lastBackpressureDropValue_ = backpressureDroppedLowPri_.value();
}

void MetricsRegistry::setInflightRejectTrace(const std::string& traceId, const std::string& sessionId) {
    std::lock_guard<std::mutex> lock(exemplarMtx_);
    lastInflightRejectTrace_ = traceId;
    lastInflightRejectSession_ = sessionId;
    lastInflightRejectValue_ = inflightRejects_.value();
}

void MetricsRegistry::setIpRejectConnTrace(const std::string& traceId, const std::string& sessionId) {
    std::lock_guard<std::mutex> lock(exemplarMtx_);
    lastIpRejectConnTrace_ = traceId;
    lastIpRejectConnSession_ = sessionId;
    lastIpRejectConnValue_ = ipRejectConn_.value();
}

void MetricsRegistry::setIpRejectQpsTrace(const std::string& traceId, const std::string& sessionId) {
    std::lock_guard<std::mutex> lock(exemplarMtx_);
    lastIpRejectQpsTrace_ = traceId;
    lastIpRejectQpsSession_ = sessionId;
    lastIpRejectQpsValue_ = ipRejectQps_.value();
}

void MetricsRegistry::setMsgRejectTrace(const std::string& traceId, const std::string& sessionId, std::uint16_t msgType) {
    std::lock_guard<std::mutex> lock(exemplarMtx_);
    lastMsgRejectTrace_ = traceId;
    lastMsgRejectSession_ = sessionId;
    lastMsgRejectType_ = msgType;
    lastMsgRejectValue_ = msgRejects_[msgType].load(std::memory_order_relaxed);
}

void MetricsRegistry::setTotalErrorTrace(const std::string& traceId, const std::string& sessionId) {
    std::lock_guard<std::mutex> lock(exemplarMtx_);
    lastTotalErrorTrace_ = traceId;
    lastTotalErrorSession_ = sessionId;
    lastTotalErrorValue_ = totalErrors_.value();
}

void MetricsRegistry::setFrameLatencyTrace(const std::string& traceId, const std::string& sessionId, double latencyMs) {
    std::lock_guard<std::mutex> lock(exemplarMtx_);
    lastFrameLatencyTrace_ = traceId;
    lastFrameLatencySession_ = sessionId;
    lastFrameLatencyMs_ = latencyMs;
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
    // -----------------------------------------------------------------
    // 1. 准备阶段：快照读取 (Snapshot)
    //    一次加锁，把所有需要 Exemplar 的数据复制出来，最大程度减少锁竞争
    // -----------------------------------------------------------------
    // 定义结构体
    struct ExemplarData {
        std::string trace;
        std::string sess;
        int64_t val = 1;  // 改成 int64_t 或 uint64_t
    };

    // 定义变量 (此时调用默认构造)
    ExemplarData errEx, bpEx, inflightEx, tokenEx, concurEx, ipConnEx, ipQpsEx, msgRejEx;
    std::uint16_t msgRejTypeSnapshot = 0;
    std::string frameTraceSnapshot;
    std::string frameSessSnapshot;
    double frameMsSnapshot = 0.0;

    {
        std::lock_guard<std::mutex> lock(exemplarMtx_);

        // 【修正点】：加上类型名 ExemplarData{...}
        if (!lastTotalErrorTrace_.empty())
            errEx = ExemplarData{lastTotalErrorTrace_, lastTotalErrorSession_, lastTotalErrorValue_};

        if (!lastBackpressureTrace_.empty())
            bpEx = ExemplarData{lastBackpressureTrace_, lastBackpressureSession_, lastBackpressureDropValue_};

        if (!lastInflightRejectTrace_.empty())
            inflightEx = ExemplarData{lastInflightRejectTrace_, lastInflightRejectSession_, lastInflightRejectValue_};

        if (!lastTokenRejectTrace_.empty())
            tokenEx = ExemplarData{lastTokenRejectTrace_, lastTokenRejectSession_, lastTokenRejectValue_};

        if (!lastConcurrentRejectTrace_.empty())
            concurEx = ExemplarData{lastConcurrentRejectTrace_, lastConcurrentRejectSession_, lastConcurrentRejectValue_};

        if (!lastIpRejectConnTrace_.empty())
            ipConnEx = ExemplarData{lastIpRejectConnTrace_, lastIpRejectConnSession_, lastIpRejectConnValue_};

        if (!lastIpRejectQpsTrace_.empty())
            ipQpsEx = ExemplarData{lastIpRejectQpsTrace_, lastIpRejectQpsSession_, lastIpRejectQpsValue_};

        if (!lastMsgRejectTrace_.empty()) {
            msgRejEx = ExemplarData{lastMsgRejectTrace_, lastMsgRejectSession_, lastMsgRejectValue_};
            msgRejTypeSnapshot = lastMsgRejectType_;
        }

        if (!lastFrameLatencyTrace_.empty()) {
            frameTraceSnapshot = lastFrameLatencyTrace_;
            frameSessSnapshot = lastFrameLatencySession_;
            frameMsSnapshot = lastFrameLatencyMs_;
        }
    }

    // -----------------------------------------------------------------
    // 2. 辅助 Lambda：统一打印逻辑
    // -----------------------------------------------------------------
    auto printMetric = [&](const std::string& name, const std::string& type, uint64_t metricValue, const ExemplarData& ex) {
        os << "# TYPE " << name << " " << type << "\n";
        os << name << " " << metricValue;

        // 如果快照里有 TraceID，说明有样本
        if (!ex.trace.empty()) {
            os << " # {trace_id=\"" << ex.trace << "\"";
            if (!ex.sess.empty()) {
                os << ",session_id=\"" << ex.sess << "\"";
            }
            // 输出你存储的 value
            os << "} " << ex.val;
        }
        os << "\n";
    };

    // -----------------------------------------------------------------
    // 3. 打印核心指标 (使用快照数据)
    // -----------------------------------------------------------------
    printMetric("server_total_errors", "counter", totalErrors_.value(), errEx);
    printMetric("server_backpressure_drop_lowpri", "counter", backpressureDroppedLowPri_.value(), bpEx);
    printMetric("server_inflight_rejects_total", "counter", inflightRejects_.value(), inflightEx);
    printMetric("server_token_rejects_total", "counter", tokenRejects_.value(), tokenEx);
    printMetric("server_concurrent_rejects_total", "counter", concurrentRejects_.value(), concurEx);
    printMetric("server_ip_reject_conn_total", "counter", ipRejectConn_.value(), ipConnEx);
    printMetric("server_ip_reject_qps_total", "counter", ipRejectQps_.value(), ipQpsEx);

    // -----------------------------------------------------------------
    // 4. 打印常规指标 (无 Exemplar，传空对象)
    // -----------------------------------------------------------------
    ExemplarData emptyEx;
    printMetric("server_connections", "gauge", connections_.value(), emptyEx);
    printMetric("server_total_frames", "counter", totalFrames_.value(), emptyEx);
    printMetric("server_bytes_in", "counter", bytesIn_.value(), emptyEx);
    printMetric("server_bytes_out", "counter", bytesOut_.value(), emptyEx);
    printMetric("server_dropped_frames", "counter", droppedFrames_.value(), emptyEx);
    printMetric("server_backpressure_triggered_total", "counter", backpressureTriggered_.value(), emptyEx);
    printMetric("server_backpressure_active", "gauge", backpressureActive_.value(), emptyEx);
    printMetric("server_backpressure_duration_ms", "counter", backpressureDurationMs_.value(), emptyEx);
    printMetric("server_send_queue_max_bytes", "gauge", sendQueueMaxBytes_.value(), emptyEx);
    printMetric("server_worker_queue_size", "gauge", workerQueueSize_.value(), emptyEx);
    printMetric("server_worker_live_threads", "gauge", workerLiveThreads_.value(), emptyEx);
    printMetric("server_inflight_frames", "gauge", inflightFrames_.value(), emptyEx);

    // -----------------------------------------------------------------
    // 5. Map 和 Histogram 保持原样
    // -----------------------------------------------------------------
    if (!msgRejects_.empty()) {
        os << "# TYPE server_msg_reject_total counter\n";
        std::lock_guard<std::mutex> lock(msgRejectsMtx_);
        for (const auto& kv : msgRejects_) {
            os << "server_msg_reject_total{msgType=\"" << kv.first << "\"} " << kv.second.load(std::memory_order_relaxed);
            if (!msgRejEx.trace.empty() && kv.first == msgRejTypeSnapshot) {
                os << " # {trace_id=\"" << msgRejEx.trace << "\"";
                if (!msgRejEx.sess.empty()) {
                    os << ",session_id=\"" << msgRejEx.sess << "\"";
                }
                os << "} " << msgRejEx.val;
            }
            os << "\n";
        }
        os << "\n";
    }

    frameLatency_.printPrometheus("server_frame_latency_ms", os);
    if (!frameTraceSnapshot.empty()) {
        os << "server_frame_latency_ms_sum " << frameMsSnapshot << " # {trace_id=\"" << frameTraceSnapshot << "\"";
        if (!frameSessSnapshot.empty()) {
            os << ",session_id=\"" << frameSessSnapshot << "\"";
        }
        os << "} " << frameMsSnapshot << "\n";
    }
    os << "# EOF\n";
}
