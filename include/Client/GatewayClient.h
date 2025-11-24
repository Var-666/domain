#pragma once

#include <boost/asio.hpp>
#include <google/protobuf/message.h>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>

// 同步客户端 SDK：封装长度头协议，支持 Raw/JSON/Proto。
class GatewayClient {
  public:
    GatewayClient();
    ~GatewayClient();

    // 连接 / 关闭
    void connect(const std::string& host, unsigned short port);
    void close();

    // 发送并阻塞接收一条回应
    std::pair<std::uint16_t, std::string> sendRaw(std::uint16_t msgType, const std::string& body);
    std::pair<std::uint16_t, std::string> sendJson(std::uint16_t msgType, const nlohmann::json& json);
    std::pair<std::uint16_t, std::string> sendProto(std::uint16_t msgType, const google::protobuf::Message& msg);

  private:
    void ensureConnected();
    std::pair<std::uint16_t, std::string> readFrame();

  private:
    boost::asio::io_context io_;
    boost::asio::ip::tcp::socket socket_;
    bool connected_{false};
};
