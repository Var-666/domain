#include "ThreadPool.h"

ThreadPool::ThreadPool(std::size_t numThreads, std::size_t maxQueueSize)
    : stopping_(false), maxQueueSize_(maxQueueSize) {
    if (numThreads <= 0) {
        numThreads = std::thread::hardware_concurrency();
    }
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

void ThreadPool::workerLoop() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this]() { return stopping_ || totalQueueSize_ > 0; });
            if (stopping_ && totalQueueSize_ == 0)
                return;

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
