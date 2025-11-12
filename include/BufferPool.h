#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <vector>

#include "Buffer.h"

class BufferPool {
  public:
    struct Deleter;
    using Ptr = std::unique_ptr<Buffer, Deleter>;

    static BufferPool& Instance();

    // 从池中获取一个 Buffer，至少保证可写 minWritable 字节
    Ptr acquire(std::size_t minWritable = 0);

    // 预热：预先放入 n 个指定初始容量的 Buffer
    void warmup(std::size_t n, std::size_t capacityHint = 4096);

    // 收缩全局缓存到不超过 keep 个
    void trim(std::size_t keep);

    // 统计
    std::size_t cachedCount() const;
    std::size_t maxCached() const;
    void setMaxCached(std::size_t m);

    std::size_t defaultCapacity() const;
    void setDefaultCapacity(std::size_t c);

    std::size_t shrinkThreshold() const;
    void setShrinkThreshold(std::size_t t);

    struct Deleter {
        void operator()(Buffer* p) const noexcept {
            if (p) {
                BufferPool::Instance().release(p);
            }
        }
    };

  private:
    BufferPool() = default;
    ~BufferPool() = default;

    void release(Buffer* buf);

    Buffer* tlTake();
    bool tlPut(Buffer* b);

  private:
    mutable std::mutex mtx_;
    std::vector<std::unique_ptr<Buffer>> globalFree_;
    std::atomic<std::size_t> cached_{0};
    std::size_t maxCached_{4096};

    std::size_t defaultCapacity_{4096};
    std::size_t shrinkThreshold_ = 1 << 20;

    static constexpr std::size_t kTLMax{8};
    struct ThreadLocalCache {
        std::size_t size{0};
        Buffer* slots[kTLMax] = {nullptr};
    };
    static thread_local ThreadLocalCache tlcache_;
};