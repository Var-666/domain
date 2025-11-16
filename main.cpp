#include <spdlog/spdlog.h>

#include <iostream>
#include <memory>

#include "AsioServer.h"
#include "Codec.h"
#include "Config.h"
#include "Logging.h"
#include "MessageLimiter.h"
#include "MessageRouter.h"
#include "Middlewares.h"
#include "Routes/CoreRoutes.h"
#include "Routes/RouteRegistry.h"

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

        auto router = std::make_shared<MessageRouter>();

        // 统一中间件注册
        RegisterMiddlewares(*router, cfg);

        // 路由集中注册
        RouteRegistry routes;
        CoreRoutes::Register(routes);
        routes.applyTo(*router);

        router->setDefaultHandler([](const ConnectionPtr& conn, uint16_t msgType, const std::string& body) {
            SPDLOG_WARN("Unknown msgType={} bodySize={}", msgType, body.size());
        });

        //  Router 接入 Codec/Server（跟你现在的用法一致）
        auto frameCb = [router](const ConnectionPtr& conn, uint16_t msgType, const std::string& body) {
            router->onMessage(conn, msgType, body);
        };
        auto codec = std::make_shared<LengthHeaderCodec>(frameCb);

        // 注册业务回调
        server.setMessageCallback([codec](const ConnectionPtr& conn, const std::string& msg) {
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