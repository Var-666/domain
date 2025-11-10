#include <iostream>

#include "AsioServer.h"

int main() {
    try {
        AsioServer server(8080, 2, 4);

        // 注册业务回调：这里做一个简单的 echo + 打印
        server.setMessageCallback([](const ConnectionPtr& conn, const std::string& msg) {
            std::cout << "[onMessage] recv: " << msg << "\n";
            // 这里已经在 worker 线程里，可以做复杂逻辑
            conn->send("echo: " + msg);
        });

        server.setCloseCallback([](const ConnectionPtr& /*conn*/) { std::cout << "[onClose] connection closed\n"; });

        std::cout << "Asio server with Connection/ThreadPool started.\n";
        server.run();
    } catch (const std::exception& ex) {
        std::cerr << "Exception: " << ex.what() << "\n";
    }

    std::cout << "Hello, World!" << std::endl;
}