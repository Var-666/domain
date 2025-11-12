#include <boost/asio.hpp>
#include <iostream>

#include "Codec.h"  // 只用它的 encodeFrame，不要重定义 ConnectionPtr

using boost::asio::ip::tcp;

int main() {
    try {
        boost::asio::io_context io;
        tcp::socket socket(io);

        tcp::endpoint ep(boost::asio::ip::make_address("8.159.139.110"), 8080);
        socket.connect(ep);
        std::cout << "Connected to server.\n";

        // 直接复用同一个 Codec 的打包逻辑
        std::string frame1 = LengthHeaderCodec::encodeFrame(100, "hello server");
        boost::asio::write(socket, boost::asio::buffer(frame1));
        std::cout << "[client] send msgType=100 body='hello server'\n";

        std::string frame2 = LengthHeaderCodec::encodeFrame(200, "foo");
        std::string frame3 = LengthHeaderCodec::encodeFrame(201, "bar");
        std::string big = frame2 + frame3;  // 粘包测试
        boost::asio::write(socket, boost::asio::buffer(big));
        std::cout << "[client] send 2 frames together\n";



        // 收点服务器的回包（简单示意）
        char buf[4096];
        boost::system::error_code ec;
        std::size_t n = socket.read_some(boost::asio::buffer(buf), ec);
        if (!ec) {
            std::cout << "[client] raw recv bytes: " << n << "\n";
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
        socket.close();
    } catch (const std::exception& ex) {
        std::cerr << "Exception: " << ex.what() << "\n";
    }
}