#include "BufferPool.h"

thread_local BufferPool::ThreadLocalCache BufferPool::tlcache_{};

BufferPool& BufferPool::Instance() {
    static BufferPool instance;
    return instance;
}

BufferPool::Ptr BufferPool::acquire(std::size_t minWritable) {
    // 尝试线程本地命中（无锁）
    if (Buffer* b = tlTake()) {
        if (minWritable) {
            b->ensureWritableBytes(minWritable);
        }
        return Ptr(b, Deleter{});
    }

    // 全局缓存（有锁）
    std::unique_ptr<Buffer> owned;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (!globalFree_.empty()) {
            owned = std::move(globalFree_.back());
            globalFree_.pop_back();
            cached_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    Buffer* raw = nullptr;
    if (owned) {
        raw = owned.release();
        raw->retrieveAll();
    } else {
        raw = new Buffer(defaultCapacity_);
    }

    if (minWritable) {
        raw->ensureWritableBytes(minWritable);
    }
    return Ptr(raw, Deleter{});
}

void BufferPool::warmup(std::size_t n, std::size_t capacityHint) {
    std::lock_guard<std::mutex> lock(mtx_);
    const std::size_t canPush = (maxCached_ > cached_.load() ? maxCached_ - cached_.load() : 0);
    n = std::min(n, canPush);
    for (std::size_t i = 0; i < n; ++i) {
        auto b = std::make_unique<Buffer>(capacityHint);
        globalFree_.push_back(std::move(b));
    }
    cached_.fetch_add(n, std::memory_order_relaxed);
}

void BufferPool::trim(std::size_t keep) {
    std::lock_guard<std::mutex> lock(mtx_);
    while (globalFree_.size() > keep) {
        globalFree_.pop_back();
        cached_.fetch_sub(1, std::memory_order_relaxed);
    }
}

std::size_t BufferPool::cachedCount() const { return cached_.load(std::memory_order_relaxed) + tlcache_.size; }

std::size_t BufferPool::maxCached() const { return maxCached_; }

void BufferPool::setMaxCached(std::size_t m) { maxCached_ = (m == 0 ? 1 : m); }

std::size_t BufferPool::defaultCapacity() const { return defaultCapacity_; }

void BufferPool::setDefaultCapacity(std::size_t c) { defaultCapacity_ = std::max<std::size_t>(256, c); }

std::size_t BufferPool::shrinkThreshold() const { return shrinkThreshold_; }

void BufferPool::setShrinkThreshold(std::size_t t) { shrinkThreshold_ = std::max(defaultCapacity_, t); }

void BufferPool::release(Buffer* buf) {
    if (!buf) {
        return;
    }
    // 连接层应保证放回前完成读取，这里统一复位
    buf->retrieveAll();
    if (shrinkThreshold_ > 0) {
        // 通过一次“完全拷贝可读区”的方式 shrink（你之前的 Buffer 实现已提供）
        // 由于我们刚 retrieveAll()，这里一般 no-op；目的是处理“被扩容得很大”的情况
        // 如果你改 Buffer 支持直接检查/获取 capacity，则可更精准地决定是否 shrink。
        // 为了稳妥，我们在这里仅在 retrieveAll() 后跳过 shrink，避免额外分配。
        // 如果你想强制收缩大 Buffer，可以解除下行注释：
        // buf->shrinkToFit();
    }

    // 先尝试放入线程本地缓存（无锁）
    if (tlPut(buf)) {
        return;
    }

    // 放不下则归还到全局缓存（有锁，受上限控制）
    std::unique_ptr<Buffer> owned(buf);
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (cached_.load(std::memory_order_relaxed) < maxCached_) {
            globalFree_.push_back(std::move(owned));
            cached_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
    }
}

Buffer* BufferPool::tlTake() {
    auto& tl = tlcache_;
    if (tl.size > 0) {
        Buffer* b = tl.slots[--tl.size];
        tl.slots[tl.size] = nullptr;
        return b;
    }
    return nullptr;
}

bool BufferPool::tlPut(Buffer* b) {
    auto& tl = tlcache_;
    if (tl.size < kTLMax) {
        tl.slots[tl.size++] = b;
        return true;
    }
    return false;
}
