#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

struct ServerConfig {
    unsigned short port = 8080;
    std::size_t ioThreadsCount = 2;
    std::size_t workerThreadsCount = 4;
    std::uint64_t IdleTimeoutMs = 60000;
    std::size_t maxQueueSize = 10000;
    int maxInflight = 10000;
    std::size_t maxSendBufferBytes = 4 * 1024 * 1024;
};

struct ThreadPoolConfig {
    std::size_t minThreads = 2;
    std::size_t maxThreads = 8;
    std::size_t maxQueueSize = 10000;

    bool autoTune = false;

    std::size_t highWatermark = 2000;
    std::size_t lowWatermark = 0;
    int upThreshold = 3;
    int downThreshold = 10;
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

class Config {
  public:
    static Config& Instance();

    bool loadFromFile(const std::string& path);

    const ServerConfig& server() const;
    const LogConfig& log() const;
    const ThreadPoolConfig threadPool() const;
    const std::unordered_map<std::uint16_t, MsgLimitConfig>& msgLimits() const;

  private:
    Config() = default;

    bool parseLuaConfig(void* L);

  private:
    ServerConfig serverCfg_;
    LogConfig logCfg_;
    ThreadPoolConfig threadPoolCfg_;
    std::unordered_map<std::uint16_t, MsgLimitConfig> msgLimitsCfg_;
};
