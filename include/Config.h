#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>

struct ServerConfig {
    unsigned short port = 8080;
    std::size_t ioThreadsCount = 2;
    std::uint64_t IdleTimeoutMs = 60000;
    std::size_t maxQueueSize = 10000;
    int maxInflight = 10000;
    std::size_t maxSendBufferBytes = 4 * 1024 * 1024;
};

struct ThreadPoolConfig {
    std::size_t workerThreadsCount = 4;
    std::size_t minThreads = 2;
    std::size_t maxThreads = 8;
    std::size_t maxQueueSize = 10000;

    bool autoTune = false;

    std::size_t highWatermark = 2000;
    std::size_t lowWatermark = 0;
    int upThreshold = 3;
    int downThreshold = 10;
};

struct Limits {
    std::size_t maxInflight = 10000;
    std::size_t maxSendBufferBytes = 4 * 1024 * 1024;
};

struct LogConfig {
    std::string level = "info";
    std::size_t asyncQueueSize = 8192;
    std::uint64_t flushIntervalMs = 1000;

    bool consoleEnable = true;

    bool fileEnable = true;
    std::string fileBaseName = "server";
    std::size_t fileMaxSizeMb = 100;
    std::size_t fileMaxFiles = 5;
};

struct MsgLimitConfig {
    bool enabled = false;
    int maxQps = 0;
    int maxConcurrent = 0;
};

struct BackpressureConfig {
    bool rejectLowPriority = false;  // 是否在背压时直接拒绝低优先级消息
    std::unordered_set<std::uint16_t> lowPriorityMsgTypes;    // 低优先级 msgType 列表
    std::unordered_set<std::uint16_t> alwaysAllowMsgTypes;    // 即使背压也必须放行（心跳等）
    bool sendErrorFrame = true;                               // 拒绝时是否回错误帧
    std::uint16_t errorMsgType = 0xFFFF;                      // 错误帧 msgType
    std::string errorBody = "backpressure";                   // 错误帧 body
};

class Config {
  public:
    static Config& Instance();

    bool loadFromFile(const std::string& path);

    const ServerConfig& server() const;
    const LogConfig& log() const;
    const ThreadPoolConfig& threadPool() const;
    const Limits& limits() const;
    const BackpressureConfig& backpressure() const;
    const std::unordered_map<std::uint16_t, MsgLimitConfig>& msgLimits() const;

  private:
    Config() = default;

    bool parseLuaConfig(void* L);

  private:
    ServerConfig serverCfg_;
    LogConfig logCfg_;
    ThreadPoolConfig threadPoolCfg_;
    Limits limitscfg_;
    BackpressureConfig backpressureCfg_;
    std::unordered_map<std::uint16_t, MsgLimitConfig> msgLimitsCfg_;
};
