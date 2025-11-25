#include "IdleConnectionManager.h"

#include <vector>

IdleConnectionManager::IdleConnectionManager(Duration idleTimeout) : idleTimeout_(idleTimeout) {}

void IdleConnectionManager::setIdleTimeout(Duration d) { idleTimeout_ = d; }

IdleConnectionManager::Duration IdleConnectionManager::idleTimeout() const { return idleTimeout_; }

void IdleConnectionManager::add(ConnectionPtr& conn) {
    std::lock_guard<std::mutex> lock(mtx_);
    conns_.insert(conn);
}

void IdleConnectionManager::remove(const ConnectionPtr& conn) {
    std::lock_guard<std::mutex> lock(mtx_);
    conns_.erase(conn);
}

void IdleConnectionManager::check() {
    auto now = Clock::now();
    auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

    std::vector<ConnectionPtr> toClose;

    {
        std::lock_guard<std::mutex> lock(mtx_);
        for (auto& conn : conns_) {
            std::uint64_t last = conn->lastActiveMs();
            if (last == 0) {
                continue;
            }
            auto diff = static_cast<std::int64_t>(nowMs - last);
            if (diff > static_cast<std::int64_t>(idleTimeout_.count())) {
                toClose.push_back(conn);
            }
        }
    }

    for (auto& c : toClose) {
        std::cerr << "[IdleTimeout] close idle connection\n";
        c->close();
    }
}
