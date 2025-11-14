#include <spdlog/spdlog.h>

#include <iostream>
#include <memory>

#include "AsioServer.h"
#include "Codec.h"
#include "Config.h"
#include "Logging.h"

int main() {
    // 1. 加载 Lua 配置
    auto& cfg = Config::Instance();
    if (!cfg.loadFromFile("../config.lua")) {
        std::cerr << "Failed to load config.lua, use defaults.\n";
    }

    Logging::InitFromConfig();

    const auto& sc = cfg.server();

    try {
        AsioServer server(sc.port, sc.ioThreadsCount, sc.workerThreadsCount, sc.IdleTimeoutMs);

        auto FrameCallback = [](const ConnectionPtr& conn, uint16_t msgType, const std::string& body) {
            std::cout << "[Server] msgType=" << msgType << " body=" << body << "\n";
            // 简单回显
            LengthHeaderCodec::send(conn, msgType, "echo: " + body);
        };

        auto codec = std::make_shared<LengthHeaderCodec>(FrameCallback);

        // 注册业务回调：这里做一个简单的 echo + 打印
        server.setMessageCallback([codec](const ConnectionPtr& conn, const std::string& msg) {
            // 这里已经在 worker 线程里，可以做复杂逻辑
            codec->onMessage(conn, msg);
        });

        server.setCloseCallback([codec](const ConnectionPtr& conn) {
            codec->onClose(conn);
            SPDLOG_INFO("[onClose] connection closed");
        });

        server.run();
    } catch (const std::exception& ex) {
        SPDLOG_CRITICAL("Exception: {}", ex.what());
    }

    Logging::shutdown();
}