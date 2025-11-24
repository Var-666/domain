#include <spdlog/spdlog.h>

#include <iostream>
#include <memory>

#include "Config.h"
#include "CrashHandler.h"
#include "InitServer.h"
#include "Logging.h"

int main() {
    CrashHandler::init();

    //  加载 Lua 配置
    auto& cfg = Config::Instance();
    if (!cfg.loadFromFile("../config/config.lua")) {
        std::cerr << "Failed to load config/config.lua, use defaults.\n";
    }
    Logging::InitFromConfig();

    const auto& sc = cfg.server();

    try {
        InitServer server(cfg);
        server.run();
    } catch (const std::exception& ex) {
        SPDLOG_CRITICAL("Exception: {}", ex.what());
    }

    Logging::shutdown();
    CrashHandler::restoreDefault();
}
