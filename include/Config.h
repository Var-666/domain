#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

struct ServerConfig {
    unsigned short port = 8080;
    std::size_t ioThreadsCount = 2;
    std::size_t workerThreadsCount = 4;
    std::uint64_t IdleTimeoutMs = 60000;
    std::size_t maxQueueSize = 10000;
    int maxInflight = 10000;
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

class Config {
  public:
    static Config& Instance();

    bool loadFromFile(const std::string& path);

    const ServerConfig& server() const;
    const LogConfig& log() const;

  private:
    Config() = default;

    bool parseLuaConfig(void* L);

  private:
    ServerConfig serverCfg_;
    LogConfig logCfg_;
};
