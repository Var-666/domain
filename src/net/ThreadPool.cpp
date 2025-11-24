#include "ThreadPool.h"

#include "Metrics.h"
#include <spdlog/spdlog.h>

ThreadPool::ThreadPool(std::size_t numThreads, std::size_t maxQueueSize, std::size_t minThreads, std::size_t maxThreads)
    : stopping_(false),
      maxQueueSize_(maxQueueSize),
      minThreads_(minThreads ? minThreads : numThreads),
      maxThreads_(maxThreads ? maxThreads : numThreads),
      targetThreads_(numThreads) {
    if (numThreads <= 0) {
        numThreads = std::thread::hardware_concurrency();
    }
    if (minThreads_ > maxThreads_) {
        maxThreads_ = minThreads_;
    }

    if (targetThreads_ < minThreads_)
        targetThreads_ = minThreads_;
    if (targetThreads_ > maxThreads_)
        targetThreads_ = maxThreads_;

    for (size_t i = 0; i < numThreads; ++i) {
        workers_.emplace_back(&ThreadPool::workerLoop, this);
    }
}

ThreadPool::~ThreadPool() { shutdown(); }

void ThreadPool::shutdown() {
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (stopping_) {
            return;
        }
        stopping_ = true;
        autoTune_ = false;
    }
    if (adjustThread_.joinable()) {
        adjustThread_.join();
    }
    cv_.notify_all();
    for (std::thread& worker : workers_) {
        if (worker.joinable())
            worker.join();
    }
    workers_.clear();
}

std::size_t ThreadPool::maxQueueSize() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return maxQueueSize_;
}

void ThreadPool::setMxQueueSize(std::size_t n) {
    std::lock_guard<std::mutex> lock(mutex_);
    maxQueueSize_ = n;
}

std::size_t ThreadPool::queueSize() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return totalQueueSize_;
}

std::size_t ThreadPool::workerCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return targetThreads_;
}

std::size_t ThreadPool::liveWorkerCount() const { return liveWorkers_.load(std::memory_order_relaxed); }

void ThreadPool::resize(std::size_t newCount) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (stopping_)
        return;
    if (newCount < minThreads_)
        newCount = minThreads_;
    if (newCount > maxThreads_)
        newCount = maxThreads_;

    if (newCount == targetThreads_)
        return;

    std::size_t old = targetThreads_;
    if (newCount > old) {
        std::size_t add = newCount - old;
        targetThreads_ = newCount;
        SPDLOG_INFO("ThreadPool resize expand: {} -> {}", old, newCount);
        for (std::size_t i = 0; i < add; ++i) {
            workers_.emplace_back(&ThreadPool::workerLoop, this);
        }
    } else {
        std::size_t reduce = old - newCount;
        targetThreads_ = newCount;
        threadsToStop_ += reduce;
        SPDLOG_INFO("ThreadPool resize shrink: {} -> {}, threadsToStop_={}", old, newCount, threadsToStop_);
        cv_.notify_all();
    }
}

void ThreadPool::enableAutoTune(bool enable) {
    bool expected = autoTune_.load(std::memory_order_relaxed);
    if (enable == expected)
        return;
    if (enable) {
        autoTune_.store(true, std::memory_order_relaxed);
        adjustThread_ = std::thread([this] { adjustLoop(); });
    } else {
        autoTune_.store(false, std::memory_order_relaxed);
        cvAdjust_.notify_all();
        if (adjustThread_.joinable()) {
            adjustThread_.join();
        }
    }
}

void ThreadPool::setAutoTuneParams(std::size_t highWatermark, std::size_t lowWatermark, int upThreshold,
                                   int downThreshold) {
    highWatermark_ = highWatermark;
    lowWatermark_ = lowWatermark;
    upThreshold_ = upThreshold;
    downThreshold_ = downThreshold;
}

void ThreadPool::workerLoop() {
    liveWorkers_.fetch_add(1, std::memory_order_relaxed);
    MetricsRegistry::Instance().workerLiveThreads().inc();
    // 确保无论哪条 return 路径都能把计数减回去
    struct Guard {
        ThreadPool* pool;
        ~Guard() {
            pool->liveWorkers_.fetch_sub(1, std::memory_order_relaxed);
            MetricsRegistry::Instance().workerLiveThreads().inc(-1);
        }
    } guard{this};
    
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this]() { return stopping_ || totalQueueSize_ > 0; });
            // 全局停止：队列空且 stopping_，线程结束
            if (stopping_ && totalQueueSize_ == 0)
                return;

            // 缩容逻辑：没有任务但有待退出线程
            if (!stopping_ && totalQueueSize_ <= lowWatermark_ && threadsToStop_ > 0) {
                --threadsToStop_;
                return;
            }

            // 正常取任务：按优先级 High -> Normal -> Low
            if (!highQ_.empty()) {
                task = std::move(highQ_.front());
                highQ_.pop();
            } else if (!normalQ_.empty()) {
                task = std::move(normalQ_.front());
                normalQ_.pop();
            } else if (!lowQ_.empty()) {
                task = std::move(lowQ_.front());
                lowQ_.pop();
            } else {
                // 理论上 totalQueueSize_ > 0 时不应发生
                continue;
            }

            --totalQueueSize_;
            MetricsRegistry::Instance().workerQueueSize().inc(-1);
        }
        task();
    }
}

bool ThreadPool::handleOverflow(TaskPriority incomingPri) {
    if (maxQueueSize_ == 0)
        return true;

    auto dropOneFrom = [this](std::queue<std::function<void()>>& q) -> bool {
        if (!q.empty()) {
            q.pop();
            --totalQueueSize_;
            MetricsRegistry::Instance().workerQueueSize().inc(-1);
            // 这里可选：记录 "丢弃任务" 的 Metrics 或日志
            return true;
        }
        return false;
    };

    switch (incomingPri) {
        case TaskPriority::Low:
            return false;
        case TaskPriority::Normal:
            if (dropOneFrom(lowQ_)) {
                return true;
            }
            return false;
        case TaskPriority::High:
            if (dropOneFrom(lowQ_))
                return true;
            if (dropOneFrom(normalQ_))
                return true;
            return false;
    }
    return false;
}

void ThreadPool::adjustLoop() {
    int highCnt = 0;
    int lowCnt = 0;

    while (autoTune_.load(std::memory_order_relaxed)) {
        {
            std::unique_lock<std::mutex> lockAdjust(mtxAdjust_);
            cvAdjust_.wait_for(lockAdjust, std::chrono::milliseconds(500),
                               [this] { return !autoTune_.load(std::memory_order_relaxed); });
            if (!autoTune_.load(std::memory_order_relaxed))
                break;
        }
        std::size_t q = queueSize();
        std::size_t cur = liveWorkerCount();

        if (q > highWatermark_) {
            ++highCnt;
            lowCnt = 0;
        } else if (q <= lowWatermark_) {
            ++lowCnt;
            highCnt = 0;
        } else {
            highCnt = lowCnt = 0;
        }

        if (highCnt >= upThreshold_ && cur < maxThreads_) {
            resize(cur + 1);
            highCnt = 0;
        }
        if (lowCnt >= downThreshold_ && cur > minThreads_) {
            resize(cur - 1);
            lowCnt = 0;
        }
    }
}
