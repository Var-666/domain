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
    explicit ThreadPool(std::size_t numThreads, std::size_t maxQueueSize = 0, std::size_t minThreads = 0,
                        std::size_t maxThreads = 0);
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
    std::size_t workerCount() const;
    std::size_t liveWorkerCount() const;

    // 动态调整线程数：newCount 会被 clamp 到 [minThreads_, maxThreads_]
    void resize(std::size_t newCount);

    // 启用/关闭自动动态伸缩（简单策略）
    void enableAutoTune(bool enable);

    // 可以根据需要修改这些参数（也可以从配置里读取）
    void setAutoTuneParams(std::size_t highWatermark, std::size_t lowWatermark, int upThreshold, int downThreshold);

  private:
    // 线程函数
    void workerLoop();

    // 队列满时的处理策略：返回 true 表示已经通过丢弃某些任务腾出了空间
    bool handleOverflow(TaskPriority incomingPri);

    // 自动调整线程数的后台线程
    void adjustLoop();

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

    // 动态伸缩相关
    std::size_t minThreads_;
    std::size_t maxThreads_;
    std::size_t targetThreads_;
    std::size_t threadsToStop_;
    std::atomic<std::size_t> liveWorkers_{0};

    // 自动伸缩控制线程
    std::thread adjustThread_;
    std::atomic<bool> autoTune_{false};
    std::mutex mtxAdjust_;
    std::condition_variable cvAdjust_;

    // 自动调整的策略参数
    std::size_t highWatermark_{1000};
    std::size_t lowWatermark_{0};
    int upThreshold_{3};
    int downThreshold_{10};
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
