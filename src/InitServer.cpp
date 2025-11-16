#include "InitServer.h"

#include <spdlog/spdlog.h>

#include <csignal>

#include "Middlewares.h"
#include "Routes/CoreRoutes.h"
#include "Routes/RouteRegistry.h"

InitServer::InitServer(const Config& cfg) : cfg_(cfg) {
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
    auto frameCb = [router](const ConnectionPtr& conn, uint16_t msgType, const std::string& body) {
        router->onMessage(conn, msgType, body);
    };
    auto codec = std::make_shared<LengthHeaderCodec>(frameCb);
    return codec;
}

std::shared_ptr<AsioServer> InitServer::buildServer(const ServerConfig& sc,
                                                    const std::shared_ptr<LengthHeaderCodec>& codec) {
    auto server = std::make_shared<AsioServer>(sc.port, sc.ioThreadsCount, sc.workerThreadsCount, sc.IdleTimeoutMs);

    server->setMessageCallback(
        [codec](const ConnectionPtr& conn, const std::string& msg) { codec->onMessage(conn, msg); });

    server->setCloseCallback([codec](const ConnectionPtr& conn) {
        codec->onClose(conn);
        SPDLOG_INFO("[onClose] connection closed");
    });

    SPDLOG_INFO("Server built: port={}, ioThreads={}, workerThreads={}, idleTimeoutMs={}", sc.port, sc.ioThreadsCount,
                sc.workerThreadsCount, sc.IdleTimeoutMs);
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
