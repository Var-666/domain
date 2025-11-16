#include <arpa/inet.h>  // htonl/ntohl

#include <boost/asio.hpp>
#include <chrono>
#include <cstring>  // std::memcpy
#include <iostream>
#include <vector>

#include "Codec.h"
using boost::asio::ip::tcp;

namespace {
    constexpr uint16_t MSG_HEARTBEAT = 1;
    constexpr uint16_t MSG_ECHO = 2;

    void readExact(tcp::socket& socket, void* data, std::size_t len) {
        std::size_t readBytes = 0;
        boost::system::error_code ec;

        while (readBytes < len) {
            std::size_t n =
                socket.read_some(boost::asio::buffer(static_cast<char*>(data) + readBytes, len - readBytes), ec);
            if (ec) {
                throw boost::system::system_error(ec);
            }
            readBytes += n;
        }  // while
    }

    struct Frame {
        uint16_t msgType;
        std::string body;
    };

    Frame readFrame(tcp::socket& socket) {
        char lenBuf[4];
        readExact(socket, lenBuf, 4);

        uint32_t len_net = 0;
        std::memcpy(&len_net, lenBuf, sizeof(len_net));
        uint32_t len = ntohl(len_net);
        if (len < 2) {
            throw std::runtime_error("invalid frame length from server");
        }

        std::vector<char> payload(len);
        readExact(socket, payload.data(), len);

        uint16_t msgType_net = 0;
        std::memcpy(&msgType_net, payload.data(), sizeof(msgType_net));

        Frame f;
        f.msgType = ntohs(msgType_net);
        f.body.assign(payload.data() + 2, len - 2);
        return f;
    }

    void sendFrame(tcp::socket& socket, uint16_t msgType, const std::string& body) {
        std::string frame = LengthHeaderCodec::encodeFrame(msgType, body);
        boost::asio::write(socket, boost::asio::buffer(frame));
    }

}  // namespace

int main(int argc, char** argv) {
    std::string host = "8.159.139.110";
    unsigned short port = 8080;
    if (argc >= 2) {
        host = argv[1];
    }
    if (argc >= 3) {
        port = static_cast<unsigned short>(std::stoi(argv[2]));
    }

    try {
        boost::asio::io_context io;
        tcp::socket socket(io);
        tcp::endpoint ep(boost::asio::ip::make_address(host), port);
        socket.connect(ep);
        std::cout << "[client] connected " << host << ":" << port << "\n";

        // 1. 发送心跳：不期待响应
        sendFrame(socket, MSG_HEARTBEAT, "");
        std::cout << "[client] sent heartbeat (msgType=" << MSG_HEARTBEAT << ")\n";

        // 2. 发送 echo，并等待返回
        std::string payload = "hello router";
        sendFrame(socket, MSG_ECHO, payload);
        Frame resp = readFrame(socket);
        std::cout << "[client] received msgType=" << resp.msgType << " body=" << resp.body << "\n";

        // 3. 发送未知类型，触发服务端 default handler（不期待响应）
        uint16_t unknownType = 9999;
        sendFrame(socket, unknownType, "???");
        std::cout << "[client] sent unknown msgType=" << unknownType << " (check server log)\n";

        socket.close();
        std::cout << "[client] done\n";
    } catch (const std::exception& ex) {
        std::cerr << "[client] exception: " << ex.what() << "\n";
        return 1;
    }
    return 0;
}
