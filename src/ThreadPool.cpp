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

std::size_t ThreadPool::maxQueueSize() const { return maxQueueSize_; }

void ThreadPool::setMxQueueSize(std::size_t n) { maxQueueSize_ = n; }

std::size_t ThreadPool::queueSize() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tasks_.size();
}

void ThreadPool::workerLoop() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this]() { return stopping_ || !tasks_.empty(); });
            if (stopping_ && tasks_.empty())
                return;
            task = std::move(tasks_.front());
            tasks_.pop();
        }
        task();
    }
}
