#include <iostream>
#include <memory>

#include "AsioServer.h"
#include "Codec.h"

int main() {
    try {
        AsioServer server(8080, 2, 4);

        auto FrameCallback = [](const ConnectionPtr& conn, uint16_t msgType, const std::string& body) {
            std::cout << "[Server] msgType=" << msgType << " body=" << body << "\n";
            // 简单回显
            LengthHeaderCodec::send(conn, msgType, "echo: " + body);
        };

        auto codec = std::make_shared<LengthHeaderCodec>(FrameCallback);

        // 注册业务回调：这里做一个简单的 echo + 打印
        server.setMessageCallback([codec](const ConnectionPtr& conn, const std::string& msg) {
            // 这里已经在 worker 线程里，可以做复杂逻辑
            codec->onMessage(conn, msg);
        });

        server.setCloseCallback([codec](const ConnectionPtr& conn) {
            codec->onClose(conn);
            std::cout << "[onClose] connection closed\n";
        });

        server.run();
    } catch (const std::exception& ex) {
        std::cerr << "Exception: " << ex.what() << "\n";
    }
}