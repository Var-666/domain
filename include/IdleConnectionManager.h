#pragma once

#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <unordered_set>

#include "AsioConnection.h"

class IdleConnectionManager {
  public:
    using Clock = std::chrono::steady_clock;
    using Duration = std::chrono::milliseconds;

    explicit IdleConnectionManager(Duration idleTimeout);

    void setIdleTimeout(Duration d);
    Duration idleTimeout() const;

    void add(ConnectionPtr& conn);
    void remove(const ConnectionPtr& conn);

    void check();

  private:
    mutable std::mutex mtx_;
    std::unordered_set<ConnectionPtr> conns_;
    Duration idleTimeout_;
};