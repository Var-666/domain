#pragma once

#include <mutex>
#include <unordered_set>

#include "AsioConnection.h"

class ConnectionManager {
  public:
    void add(const ConnectionPtr& connection);
    void remove(const ConnectionPtr& connection);
    void clear();

    void broadcast(const std::string& message);
    std::size_t size() const;

    template <typename F>
    void forEach(F&& f) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& c : connections_) {
            f(c);
        }
    }

  private:
    std::unordered_set<ConnectionPtr> connections_;
    mutable std::mutex mutex_;
};