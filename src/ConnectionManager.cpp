#include "ConnectionManager.h"

void ConnectionManager::add(const ConnectionPtr& connection) {
    std::lock_guard<std::mutex> lock(mutex_);
    connections_.insert(connection);
}

void ConnectionManager::remove(const ConnectionPtr& connection) {
    std::lock_guard<std::mutex> lock(mutex_);
    connections_.erase(connection);
}

void ConnectionManager::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    connections_.clear();
}

void ConnectionManager::broadcast(const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& connection : connections_) {
        connection->send(message);
    }
}

std::size_t ConnectionManager::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return connections_.size();
}