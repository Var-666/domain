#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "AsioConnection.h"
#include "MessageRouter.h"

struct RouteEntry {
    std::uint16_t msgType;
    std::string name;
    MessageHandler handler;
};

class RouteRegistry {
  public:
    void add(std::uint16_t msgType, std::string name, MessageHandler handler) {
        entries_.push_back({msgType, std::move(name), std::move(handler)});
    }

    void applyTo(MessageRouter& router) const {
        for (auto& e : entries_) {
            router.registerHandler(e.msgType, e.handler);
        }
    }

    const std::vector<RouteEntry>& entries() const { return entries_; }

  private:
    std::vector<RouteEntry> entries_;
};