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

  private:
    std::unordered_set<ConnectionPtr> connections_;
    mutable std::mutex mutex_;
};