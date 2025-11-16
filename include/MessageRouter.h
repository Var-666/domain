#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "AsioConnection.h"

struct MessageContext {
    ConnectionPtr conn;
    std::uint16_t msgType;
    const std::string* body;
};

using MessageHandler = std::function<void(const ConnectionPtr&, const std::string&)>;

using NextFunc = std::function<void(MessageContext&)>;
using Middleware = std::function<void(MessageContext&, NextFunc)>;

class MessageRouter {
  public:
    MessageRouter() = default;

    // 注册一个 msgType 对应的 handler（线程安全）
    void registerHandler(std::uint16_t msgType, MessageHandler handler);

    // 设置一个默认 handler：当 msgType 未注册时调用（可选）
    void setDefaultHandler(std::function<void(const ConnectionPtr&, std::uint16_t, const std::string&)> handler);

    // 注册中间件（建议在服务器启动阶段调用）
    void use(Middleware mw);

    // Codec 解出一帧后调用
    void onMessage(const ConnectionPtr& conn, std::uint16_t msgType, const std::string& body);

  private:
    // 实际执行链：从第 idx 个 middleware 开始
    void dispatch(std::size_t idx, MessageContext& ctx);

    // 取出一个 handler（上读锁）
    MessageHandler getHandler(std::uint16_t msgType);

  private:
    mutable std::mutex mtx_;
    std::unordered_map<std::uint16_t, MessageHandler> handlers_;
    std::function<void(const ConnectionPtr&, std::uint16_t, const std::string&)> defaultHandler_;

    std::vector<Middleware> middlewares_;
};