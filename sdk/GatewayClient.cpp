#include "GatewayClient.h"

#include <arpa/inet.h>

#include <cstring>
#include <stdexcept>
#include <vector>

#include "Codec.h"

namespace {
    void readExact(boost::asio::ip::tcp::socket& socket, void* data, std::size_t len) {
        std::size_t readBytes = 0;
        boost::system::error_code ec;
        while (readBytes < len) {
            std::size_t n = socket.read_some(boost::asio::buffer(static_cast<char*>(data) + readBytes, len - readBytes), ec);
            if (ec) {
                throw boost::system::system_error(ec);
            }
            readBytes += n;
        }
    }
}  // namespace

GatewayClient::GatewayClient() : socket_(io_) {}

GatewayClient::~GatewayClient() { close(); }

void GatewayClient::connect(const std::string& host, unsigned short port) {
    boost::asio::ip::tcp::endpoint ep(boost::asio::ip::make_address(host), port);
    socket_.connect(ep);
    connected_ = true;
    boost::asio::ip::tcp::no_delay nd(true);
    socket_.set_option(nd);
}

void GatewayClient::close() {
    if (!connected_)
        return;
    boost::system::error_code ec;
    socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
    socket_.close(ec);
    connected_ = false;
}

std::pair<std::uint16_t, std::string> GatewayClient::sendRaw(std::uint16_t msgType, const std::string& body) {
    ensureConnected();
    std::string frame = LengthHeaderCodec::encodeFrame(msgType, body);
    boost::asio::write(socket_, boost::asio::buffer(frame));
    return readFrame();
}

std::pair<std::uint16_t, std::string> GatewayClient::sendJson(std::uint16_t msgType, const nlohmann::json& json) {
    return sendRaw(msgType, json.dump());
}

std::pair<std::uint16_t, std::string> GatewayClient::sendProto(std::uint16_t msgType, const google::protobuf::Message& msg) {
    std::string buf;
    if (!msg.SerializeToString(&buf)) {
        throw std::runtime_error("Serialize protobuf failed");
    }
    return sendRaw(msgType, buf);
}

void GatewayClient::ensureConnected() {
    if (!connected_) {
        throw std::runtime_error("GatewayClient not connected");
    }
}

std::pair<std::uint16_t, std::string> GatewayClient::readFrame() {
    char lenBuf[4];
    readExact(socket_, lenBuf, 4);
    uint32_t lenNet = 0;
    std::memcpy(&lenNet, lenBuf, sizeof(lenNet));
    uint32_t len = ntohl(lenNet);
    if (len < 2) {
        throw std::runtime_error("Invalid frame length");
    }

    std::vector<char> payload(len);
    readExact(socket_, payload.data(), len);

    uint16_t msgTypeNet = 0;
    std::memcpy(&msgTypeNet, payload.data(), sizeof(msgTypeNet));

    std::pair<std::uint16_t, std::string> res;
    res.first = ntohs(msgTypeNet);
    res.second.assign(payload.data() + 2, len - 2);
    return res;
}
