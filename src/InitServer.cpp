#include "InitServer.h"

#include <spdlog/spdlog.h>

#include <csignal>

#include "Buffer.h"
#include "GlobalState.h"
#include "Middlewares.h"
#include "Routes/CoreRoutes.h"
#include "Routes/RouteRegistry.h"

InitServer::InitServer(const Config& cfg) : cfg_(cfg) {
    // 1. 先建线程池（用前面 Lua 配置里的 thread_pool）
    const auto& tpc = cfg_.threadPool();

    workerPool_ =
        std::make_shared<ThreadPool>(tpc.workerThreadsCount, tpc.maxQueueSize, tpc.minThreads, tpc.maxThreads);
    workerPool_->setAutoTuneParams(tpc.highWatermark, tpc.lowWatermark, tpc.upThreshold, tpc.downThreshold);
    if (tpc.autoTune) {
        workerPool_->enableAutoTune(true);
    }

    // 按顺序构建组件
    router_ = buildRouter(cfg_);
    codec_ = buildCodec(router_);
    server_ = buildServer(cfg_.server(), codec_);

    // 启动信号监听
    startSignalWatcher();
}

InitServer::~InitServer() { stopSignalWatcher(); }

void InitServer::run() { server_->run(); }

std::shared_ptr<MessageRouter> InitServer::buildRouter(const Config& cfg) {
    auto router = std::make_shared<MessageRouter>();

    // 1. 注册中间件
    RegisterMiddlewares(*router, cfg);

    // 2. 注册所有路由（可根据项目拆模块）
    RouteRegistry routes;
    CoreRoutes::Register(routes);
    routes.applyTo(*router);

    // 3. 默认 handler
    router->setDefaultHandler([](const ConnectionPtr& conn, uint16_t msgType, const std::string& body) {
        SPDLOG_WARN("Unknown msgType={} bodySize={}", msgType, body.size());
    });

    return router;
}

std::shared_ptr<LengthHeaderCodec> InitServer::buildCodec(const std::shared_ptr<MessageRouter>& router) {
    auto workerPool = workerPool_;  // 拷贝一份 shared_ptr，用于 lambda 捕获

    auto frameCb = [router, workerPool](const ConnectionPtr& conn, uint16_t msgType, const std::string& body) {
        // 全局 in-flight 限制：按“帧”维度
        int cur = gInflight.fetch_add(1, std::memory_order_relaxed);
        if (cur >= kMaxInflight) {
            gInflight.fetch_sub(1, std::memory_order_relaxed);
            MetricsRegistry::Instance().totalErrors().inc();
            SPDLOG_ERROR("too many in-flight frames, drop msgType={}", msgType);
            return;
        }

        auto weak = std::weak_ptr<AsioConnection>(conn);
        try {
            workerPool->submit([router, weak, msgType, body]() {
                if (auto shared = weak.lock()) {
                    try {
                        router->onMessage(shared, msgType, body);
                    } catch (const std::exception& ex) {
                        SPDLOG_ERROR("router->onMessage exception: {}", ex.what());
                    } catch (...) {
                        SPDLOG_ERROR("router->onMessage unknown exception");
                    }
                }
                gInflight.fetch_sub(1, std::memory_order_relaxed);
            });
        } catch (const std::exception& ex) {
            gInflight.fetch_sub(1, std::memory_order_relaxed);
            MetricsRegistry::Instance().totalErrors().inc();
            SPDLOG_ERROR("ThreadPool submit failed in FrameCallback: {}", ex.what());
        }
    };

    auto codec = std::make_shared<LengthHeaderCodec>(frameCb);
    return codec;
}

std::shared_ptr<AsioServer> InitServer::buildServer(const ServerConfig& sc,
                                                    const std::shared_ptr<LengthHeaderCodec>& codec) {
    auto server = std::make_shared<AsioServer>(sc.port, sc.ioThreadsCount, sc.IdleTimeoutMs);

    server->setMessageCallback([codec](const ConnectionPtr& conn, Buffer& buf) { codec->onMessage(conn, buf); });

    server->setCloseCallback([codec](const ConnectionPtr& conn) {
        codec->onClose(conn);
        SPDLOG_INFO("[onClose] connection closed");
    });

    SPDLOG_INFO("Server built: port={}, ioThreads={}, idleTimeoutMs={}", sc.port, sc.ioThreadsCount, sc.IdleTimeoutMs);
    return server;
}

void InitServer::startSignalWatcher() {
    signalIo_ = std::make_shared<boost::asio::io_context>();
    signals_ = std::make_shared<boost::asio::signal_set>(*signalIo_, SIGINT, SIGTERM);

    // 捕获 SIGINT/SIGTERM，优雅关闭
    signals_->async_wait([server = server_](const boost::system::error_code& ec, int sig) {
        if (ec)
            return;

        SPDLOG_WARN("Received signal {} , start graceful shutdown", sig);

        // 1. 停止接受新连接
        server->stopAccept();

        // 2. 在 server 的 io_context 上开一个 10 秒的 timer
        auto timer = std::make_shared<boost::asio::steady_timer>(server->ioContext());
        timer->expires_after(std::chrono::seconds(10));

        timer->async_wait([server, timer](const boost::system::error_code& /*ec2*/) {
            SPDLOG_WARN("Graceful timeout reached → closing all connections...");
            server->closeAllConnections();
            server->stop();
        });
    });

    // 单独线程跑 signalIo_
    signalThread_ = std::thread([io = signalIo_]() {
        try {
            io->run();
        } catch (const std::exception& ex) {
            SPDLOG_ERROR("Signal IO exception: {}", ex.what());
        }
    });
}

void InitServer::stopSignalWatcher() {
    if (signalIo_) {
        signalIo_->stop();
    }
    if (signalThread_.joinable()) {
        signalThread_.join();
    }
    signalIo_.reset();
    signals_.reset();
}
