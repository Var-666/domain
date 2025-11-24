#include "IpLimiter.h"

#include <chrono>

#include "Config.h"
#include "Metrics.h"

IpLimiter& IpLimiter::Instance() {
    static IpLimiter inst;
    return inst;
}

void IpLimiter::updateConfig(const IpLimitConfig& cfg) {
    std::lock_guard<std::mutex> lock(mtx_);
    whitelist_ = cfg.whitelist;
    maxConnPerIp_ = cfg.maxConnPerIp;
    maxQpsPerIp_ = cfg.maxQpsPerIp;
}

bool IpLimiter::allowConn(const std::string& ip) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (whitelist_.count(ip))
        return true;
    if (maxConnPerIp_ == 0)
        return true;
    auto& c = connCount_[ip];
    if (c >= maxConnPerIp_) {
        return false;
    }
    ++c;
    return true;
}

void IpLimiter::onConnClose(const std::string& ip) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = connCount_.find(ip);
    if (it != connCount_.end()) {
        if (it->second > 0)
            --it->second;
    }
}

bool IpLimiter::allowQps(const std::string& ip) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (whitelist_.count(ip))
        return true;
    if (maxQpsPerIp_ == 0)
        return true;

    auto now = std::chrono::system_clock::now().time_since_epoch();
    std::uint64_t sec = std::chrono::duration_cast<std::chrono::seconds>(now).count();

    auto& st = qpsCount_[ip];
    if (st.windowSec != sec) {
        st.windowSec = sec;
        st.count = 0;
    }
    if (st.count >= maxQpsPerIp_) {
        return false;
    }
    ++st.count;
    return true;
}

std::size_t IpLimiter::connCount(const std::string& ip) const {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = connCount_.find(ip);
    return it == connCount_.end() ? 0 : it->second;
}
