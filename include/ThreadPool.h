#pragma once

#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
enum class TaskPriority {
    High = 0,
    Normal = 1,
    Low = 2,
};

class ThreadPool {
  public:
    explicit ThreadPool(std::size_t numThreads, std::size_t maxQueueSize = 0);
    ~ThreadPool();

    // 提交任务到线程池
    template <typename F, typename... Args>
    auto submit(TaskPriority pri, F&& f, Args&&... args) -> std::future<typename std::invoke_result<F, Args...>::type>;

    template <typename F, typename... Args>
    auto submit(F&& f, Args&&... args) -> std::future<typename std::invoke_result<F, Args...>::type>;

    void shutdown();

    std::size_t maxQueueSize() const;
    void setMxQueueSize(std::size_t n);

    std::size_t queueSize() const;

  private:
    // 线程函数
    void workerLoop();

    bool handleOverflow(TaskPriority incomingPri);

  private:
    bool stopping_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<std::thread> workers_;

    std::queue<std::function<void()>> highQ_;
    std::queue<std::function<void()>> normalQ_;
    std::queue<std::function<void()>> lowQ_;

    std::size_t maxQueueSize_{0};    // 任务队列最大数量
    std::size_t totalQueueSize_{0};  // 队列总数

};

template <typename F, typename... Args>
auto ThreadPool::submit(TaskPriority pri, F&& f, Args&&... args)
    -> std::future<typename std::invoke_result<F, Args...>::type> {
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
        if (maxQueueSize_ > 0 && totalQueueSize_ >= maxQueueSize_) {
            if (!handleOverflow(pri)) {
                throw std::runtime_error("ThreadPool queue full");
            }
        }
        // 这里一定有空间可以插入（或者 maxQueueSize_ == 0）
        switch (pri) {
            case TaskPriority::High:
                highQ_.emplace([task] { (*task)(); });
                break;
            case TaskPriority::Normal:
                normalQ_.emplace([task] { (*task)(); });
                break;
            case TaskPriority::Low:
                lowQ_.emplace([task] { (*task)(); });
                break;
        }
        ++totalQueueSize_;
    }
    cv_.notify_one();
    return fut;
}

template <typename F, typename... Args>
auto ThreadPool::submit(F&& f, Args&&... args) -> std::future<typename std::invoke_result<F, Args...>::type> {
    return submit(TaskPriority::Normal, std::forward<F>(f), std::forward<Args>(args)...);
};
