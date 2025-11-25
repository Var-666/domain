#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <google/protobuf/message.h>
#include <nlohmann/json.hpp>

#include "AsioConnection.h"

struct MessageContext {
    ConnectionPtr conn;
    std::uint16_t msgType;
    const std::string* body;
};

using MessageHandler = std::function<void(const ConnectionPtr&, const std::string&)>;
using NextFunc = std::function<void(MessageContext&)>;
using Middleware = std::function<void(MessageContext&, NextFunc)>;

enum class PayloadFormat { Raw, Json, Proto };

struct HandlerEntry {
    PayloadFormat fmt{PayloadFormat::Raw};  // 负责编码/解码的格式
    MessageHandler rawHandler;              // 直接使用 std::string 的处理器
    std::function<void(const ConnectionPtr&, const nlohmann::json&)> jsonHandler;  // JSON 处理器
    std::function<void(const ConnectionPtr&, google::protobuf::Message&)> protoHandler;  // Protobuf 处理器
    std::function<std::unique_ptr<google::protobuf::Message>()> protoFactory;  // Proto 实例工厂（用于反序列化）
};

// 消息路由器：按 msgType 分发，支持中间件链、JSON/Proto/Raw 三种格式。
class MessageRouter {
  public:
    MessageRouter() = default;

    // 注册一个 msgType 对应的 raw handler（线程安全）
    void registerHandler(std::uint16_t msgType, MessageHandler handler);
    // 注册 JSON handler
    void registerJson(std::uint16_t msgType, std::function<void(const ConnectionPtr&, const nlohmann::json&)> handler);
    // 注册 Protobuf handler，模板推导 proto 类型
    template <typename ProtoT>
    void registerProto(std::uint16_t msgType, std::function<void(const ConnectionPtr&, const ProtoT&)> handler);

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
    HandlerEntry getHandler(std::uint16_t msgType);

  private:
    mutable std::mutex mtx_;  // 保护 handlers_/middlewares_
    std::unordered_map<std::uint16_t, HandlerEntry> handlers_;  // msgType -> handler
    std::function<void(const ConnectionPtr&, std::uint16_t, const std::string&)> defaultHandler_;  // 未注册 msgType 的兜底处理

    std::vector<Middleware> middlewares_;
};

template <typename ProtoT>
void MessageRouter::registerProto(std::uint16_t msgType, std::function<void(const ConnectionPtr&, const ProtoT&)> handler) {
    HandlerEntry entry;
    entry.fmt = PayloadFormat::Proto;
    entry.protoFactory = []() { return std::make_unique<ProtoT>(); };
    entry.protoHandler = [h = std::move(handler)](const ConnectionPtr& conn, google::protobuf::Message& msg) {
        auto* typed = dynamic_cast<ProtoT*>(&msg);
        if (typed) {
            h(conn, *typed);
        }
    };
    std::lock_guard<std::mutex> lock(mtx_);
    handlers_[msgType] = std::move(entry);
}
