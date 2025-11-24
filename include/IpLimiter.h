#pragma once

#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

struct IpLimitConfig;

class IpLimiter {
  public:
    static IpLimiter& Instance();

    void updateConfig(const IpLimitConfig& cfg);

    bool allowConn(const std::string& ip);
    void onConnClose(const std::string& ip);

    bool allowQps(const std::string& ip);

    std::size_t connCount(const std::string& ip) const;

  private:
    IpLimiter() = default;

    struct QpsState {
        std::uint64_t windowSec{0};
        std::size_t count{0};
    };

    mutable std::mutex mtx_;
    std::unordered_set<std::string> whitelist_;
    std::size_t maxConnPerIp_{0};
    std::size_t maxQpsPerIp_{0};
    std::unordered_map<std::string, std::size_t> connCount_;
    std::unordered_map<std::string, QpsState> qpsCount_;
};
