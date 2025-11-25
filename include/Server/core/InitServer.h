#pragma once

#include <boost/asio.hpp>
#include <memory>
#include <thread>
#include <atomic>

#include "AsioServer.h"
#include "Codec.h"
#include "Config.h"
#include "HttpControlServer.h"
#include "MessageRouter.h"

class InitServer {
  public:
    explicit InitServer(const Config& cfg);

    ~InitServer();

    void run();

  private:
    std::shared_ptr<MessageRouter> buildRouter(const Config& cfg);
    std::shared_ptr<LengthHeaderCodec> buildCodec(const std::shared_ptr<MessageRouter>& router, const Config& cfg);
    std::shared_ptr<AsioServer> buildServer(const ServerConfig& sc, const std::shared_ptr<LengthHeaderCodec>& codec);
    std::shared_ptr<HttpControlServer> buildHttpControlServer(const Config& cfg);

    void startSignalWatcher();
    void stopSignalWatcher();

  private:
    const Config& cfg_;

    std::shared_ptr<MessageRouter> router_;
    std::shared_ptr<LengthHeaderCodec> codec_;
    std::shared_ptr<AsioServer> server_;
    std::shared_ptr<HttpControlServer> httpServer_;

    // 信号监听 io + 线程
    std::shared_ptr<boost::asio::io_context> signalIo_;
    std::shared_ptr<boost::asio::signal_set> signals_;
    std::thread signalThread_;

    std::shared_ptr<ThreadPool> workerPool_;
    std::atomic<int> inflight_{0};
};
