#include "Logging.h"

#include <spdlog/async.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <spdlog/pattern_formatter.h>

#include <vector>

#include "Config.h"
#include "TraceContext.h"

namespace Logging {
    static spdlog::level::level_enum ParseLevel(const std::string& lvl) {
        std::string s = lvl;
        for (auto& c : s) {
            c = std::tolower(c);
        }

        if (s == "trace")
            return spdlog::level::trace;
        if (s == "debug")
            return spdlog::level::debug;
        if (s == "info")
            return spdlog::level::info;
        if (s == "warn")
            return spdlog::level::warn;
        if (s == "error")
            return spdlog::level::err;
        if (s == "critical")
            return spdlog::level::critical;
        if (s == "off")
            return spdlog::level::off;
        return spdlog::level::info;
    }

    void InitFromConfig() {
        const auto& cfg = Config::Instance().log();
        // 初始化异步线程池
        // queue_size = asyncQueueSize, 线程数先用 1 个足够
        spdlog::init_thread_pool(cfg.asyncQueueSize, 1);

        std::vector<spdlog::sink_ptr> sinks;

        if (cfg.consoleEnable) {
            auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");
            sinks.push_back(console_sink);
        }

        if (cfg.fileEnable) {
            auto max_size = cfg.fileMaxSizeMb * 1024 * 1024;
            auto rotating_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                cfg.fileBaseName + ".log", max_size, static_cast<std::size_t>(cfg.fileMaxFiles));
            rotating_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] %v");
            sinks.push_back(rotating_sink);
        }

        if (sinks.empty()) {
            sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
        }

        auto logger = std::make_shared<spdlog::logger>("main_logger", sinks.begin(), sinks.end());
        logger->set_level(ParseLevel(cfg.level));
        logger->flush_on(spdlog::level::warn);

        // sinks 会 clone formatter
        spdlog::set_default_logger(logger);
        spdlog::set_level(ParseLevel(cfg.level));

        spdlog::flush_every(std::chrono::milliseconds(cfg.flushIntervalMs));
    }
    void shutdown() { spdlog::shutdown(); }

}  // namespace Logging
