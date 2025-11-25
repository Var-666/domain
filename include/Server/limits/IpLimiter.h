#pragma once

#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

struct IpLimitConfig;

/**
 * @brief Per-IP 限流器（连接数/QPS）。
 *
 * 目标：防止单个 IP 的连接洪泛或高频请求压垮服务。
 * 特性：
 *  - 白名单：跳过限流。
 *  - 连接数限制：accept 阶段拒绝超限连接。
 *  - QPS 限制：秒级窗口计数，超限直接拒绝该帧。
 *  - 状态 TTL：按最近访问时间懒触发 GC，避免状态无限增长。
 * 线程安全：内部加锁，所有接口可并发调用。
 */
class IpLimiter {
  public:
    // 获取单例
    static IpLimiter& Instance();

    // 从配置更新限流阈值/白名单/TTL
    void updateConfig(const IpLimitConfig& cfg);

    // 检查并计数：是否允许新连接
    bool allowConn(const std::string& ip);
    // 连接关闭时归还计数
    void onConnClose(const std::string& ip);

    // 检查并计数：是否允许当前请求（QPS）
    bool allowQps(const std::string& ip);

    // 获取某 IP 当前连接数（仅用于观测/测试）
    std::size_t connCount(const std::string& ip) const;

  private:
    // 根据 TTL 触发一次状态清理（懒触发，避免频繁扫描）
    void gcIfNeeded(std::uint64_t nowSec);
    // 记录 IP 最近访问时间（用于 GC 判定）
    void touch(const std::string& ip, std::uint64_t nowSec);

    IpLimiter() = default;

    struct QpsState {
        std::uint64_t windowSec{0};   // 当前秒级窗口
        std::size_t count{0};         // 窗口内计数
        std::uint64_t lastAccess{0};  // 最近访问时间（秒）
    };

    mutable std::mutex mtx_;                          // 保护内部状态
    std::unordered_set<std::string> whitelist_;       // 白名单 IP
    std::size_t maxConnPerIp_{0};                     // 每 IP 最大连接数
    std::size_t maxQpsPerIp_{0};                      // 每 IP 最大 QPS
    std::uint64_t stateTtlSec_{300};                  // 状态 TTL（秒）
    std::uint64_t lastGcSec_{0};                      // 上次 GC 时间（秒）
    std::unordered_map<std::string, std::size_t> connCount_;  // IP → 连接数
    std::unordered_map<std::string, QpsState> qpsCount_;      // IP → QPS 状态
};
