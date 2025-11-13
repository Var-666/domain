#pragma once

#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

class ThreadPool {
  public:
    explicit ThreadPool(std::size_t numThreads, std::size_t maxQueueSize = 0);
    ~ThreadPool();

    // 提交任务到线程池
    template <typename F, typename... Args>
    auto submit(F&& f, Args&&... args) -> std::future<typename std::invoke_result<F, Args...>::type>;

    void shutdown();

    std::size_t maxQueueSize() const;
    void setMxQueueSize(std::size_t n);

    std::size_t queueSize() const;

  private:
    // 线程函数
    void workerLoop();

  private:
    bool stopping_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;

    std::size_t maxQueueSize_{0};  // 任务队列最大数量
};

template <typename F, typename... Args>
auto ThreadPool::submit(F&& f, Args&&... args) -> std::future<typename std::invoke_result<F, Args...>::type> {
    using ReturnType = typename std::invoke_result<F, Args...>::type;
    // 创建一个打包任务
    auto task = std::make_shared<std::packaged_task<ReturnType()>>(
        [fun = std::forward<F>(f), ... arg = std::forward<Args>(args)]() { return fun(arg...); });
    std::future<ReturnType> fut = task->get_future();
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (stopping_) {
            throw std::runtime_error("Submit on stopped ThreadPool");
        }
        // 队列上限 + 过载策略
        if (maxQueueSize_ > 0 && tasks_.size() >= maxQueueSize_) {
            // 策略1：直接抛异常
            throw std::runtime_error("ThreadPool queue full");

            // 策略2：CallerRuns
            // lock.unlock(); (*taskPtr)(); return fut;
        }
        tasks_.emplace([task]() { (*task)(); });
    }
    cv_.notify_one();
    return fut;
}
