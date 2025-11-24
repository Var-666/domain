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
    stateTtlSec_ = cfg.stateTtlSec;
}

bool IpLimiter::allowConn(const std::string& ip) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (whitelist_.count(ip))
        return true;
    if (maxConnPerIp_ == 0)
        return true;
    auto nowSec = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                                 std::chrono::system_clock::now().time_since_epoch())
                                                 .count());
    gcIfNeeded(nowSec);

    auto& c = connCount_[ip];
    if (c >= maxConnPerIp_) {
        return false;
    }
    ++c;
    touch(ip, nowSec);
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

    gcIfNeeded(sec);

    auto& st = qpsCount_[ip];
    if (st.windowSec != sec) {
        st.windowSec = sec;
        st.count = 0;
    }
    if (st.count >= maxQpsPerIp_) {
        return false;
    }
    ++st.count;
    st.lastAccess = sec;
    return true;
}

std::size_t IpLimiter::connCount(const std::string& ip) const {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = connCount_.find(ip);
    return it == connCount_.end() ? 0 : it->second;
}

void IpLimiter::gcIfNeeded(std::uint64_t nowSec) {
    if (stateTtlSec_ == 0)
        return;
    if (lastGcSec_ != 0 && nowSec - lastGcSec_ < stateTtlSec_)
        return;
    lastGcSec_ = nowSec;

    auto gcMap = [this, nowSec](auto& mp, auto accessor) {
        for (auto it = mp.begin(); it != mp.end();) {
            std::uint64_t last = accessor(it->second);
            if (nowSec - last > stateTtlSec_) {
                it = mp.erase(it);
            } else {
                ++it;
            }
        }
    };

    // 清理长时间无访问且无连接计数的 IP
    for (auto it = connCount_.begin(); it != connCount_.end();) {
        auto qit = qpsCount_.find(it->first);
        std::uint64_t last = 0;
        if (qit != qpsCount_.end()) {
            last = qit->second.lastAccess;
        }
        bool expired = (last > 0 && nowSec - last > stateTtlSec_) && it->second == 0;
        if (expired) {
            it = connCount_.erase(it);
            if (qit != qpsCount_.end()) {
                qpsCount_.erase(qit);
            }
        } else {
            ++it;
        }
    }
    gcMap(qpsCount_, [](const QpsState& st) { return st.lastAccess; });
}

void IpLimiter::touch(const std::string& ip, std::uint64_t nowSec) {
    auto it = qpsCount_.find(ip);
    if (it != qpsCount_.end()) {
        it->second.lastAccess = nowSec;
    } else {
        QpsState st;
        st.lastAccess = nowSec;
        qpsCount_[ip] = st;
    }
}
