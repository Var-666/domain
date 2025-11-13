#include <arpa/inet.h>  // htonl/ntohl

#include <atomic>
#include <boost/asio.hpp>
#include <chrono>
#include <cstring>  // std::memcpy
#include <iostream>
#include <thread>
#include <vector>

#include "Codec.h"  // 只用 LengthHeaderCodec::encodeFrame

using boost::asio::ip::tcp;

// 读取指定字节数
static void readExact(tcp::socket& socket, void* data, std::size_t len) {
    std::size_t readBytes = 0;
    boost::system::error_code ec;

    while (readBytes < len) {
        std::size_t n =
            socket.read_some(boost::asio::buffer(static_cast<char*>(data) + readBytes, len - readBytes), ec);
        if (ec) {
            throw boost::system::system_error(ec);
        }
        readBytes += n;
    }
}

static void readOneFrame(tcp::socket& socket) {
    // 1. 先读 4 字节 len
    char lenBuf[4];
    readExact(socket, lenBuf, 4);

    uint32_t len_net = 0;
    std::memcpy(&len_net, lenBuf, 4);
    uint32_t len = ntohl(len_net);  // = 2 + bodyLen

    if (len < 2) {
        throw std::runtime_error("invalid frame length from server");
    }

    // 2. 再读 len 字节：前 2 字节是 msgType，后面是 body
    std::vector<char> payload(len);
    readExact(socket, payload.data(), len);

    // 如需解析：
    uint16_t msgType_net = 0;
    std::memcpy(&msgType_net, payload.data(), 2);
    uint16_t msgType = ntohs(msgType_net);
    std::string body(payload.data() + 2, len - 2);

    // 这里只是为了 RTT，不用实际用 body 也行
    // std::cout << "[client] resp msgType=" << msgType << " body=" << body << "\n";
}

// 全局统计
struct Stats {
    std::atomic<long long> total_requests{0};
    std::atomic<long long> total_latency_ns{0};  // 累积 RTT（纳秒）
};

// 压测线程函数：每个线程一个连接，持续发包
void workerThreadFunc(const std::string& host, unsigned short port, int thread_id, int msgType, const std::string& body,
                      std::chrono::seconds duration, Stats& stats) {
    try {
        boost::asio::io_context io;
        tcp::socket socket(io);

        tcp::endpoint ep(boost::asio::ip::make_address(host), port);
        socket.connect(ep);
        std::cout << "[worker " << thread_id << "] connected\n";

        auto endTime = std::chrono::steady_clock::now() + duration;

        while (std::chrono::steady_clock::now() < endTime) {
            // 编码请求 frame
            std::string frame = LengthHeaderCodec::encodeFrame(msgType, body);

            // 记录开始时间
            auto t0 = std::chrono::steady_clock::now();

            // 发送
            boost::asio::write(socket, boost::asio::buffer(frame));

            // 阻塞等待一个完整响应帧（对应 echo 场景）
            readOneFrame(socket);

            // 记录结束时间
            auto t1 = std::chrono::steady_clock::now();
            auto rtt_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();

            stats.total_requests.fetch_add(1, std::memory_order_relaxed);
            stats.total_latency_ns.fetch_add(rtt_ns, std::memory_order_relaxed);
        }

        socket.close();
        std::cout << "[worker " << thread_id << "] finished\n";
    } catch (const std::exception& ex) {
        std::cerr << "[worker " << thread_id << "] exception: " << ex.what() << "\n";
    }
}

int main() {
    // ===== 配置区域 =====
    std::string host = "8.159.139.110";  // 你的服务器 IP
    std::string lchost = "127.0.0.1";
    unsigned short port = 8080;  // 服务器端口

    int threads = 4;                           // 并发连接/线程数
    auto duration = std::chrono::seconds(10);  // 压测时长

    int msgType = 100;                     // 就用一个固定类型
    std::string body = "hello benchmark";  // 请求体内容
    // ===================

    Stats stats;

    std::cout << "Benchmark start: host=" << host << " port=" << port << " threads=" << threads
              << " duration=" << duration.count() << "s\n";

    auto startTime = std::chrono::steady_clock::now();

    // 启动 worker 线程
    std::vector<std::thread> workers;
    workers.reserve(threads);
    for (int i = 0; i < threads; ++i) {
        workers.emplace_back(workerThreadFunc, host, port, i, msgType, body, duration, std::ref(stats));
    }

    // 等待所有线程结束
    for (auto& t : workers) {
        if (t.joinable())
            t.join();
    }

    auto endTime = std::chrono::steady_clock::now();
    double elapsed_s = std::chrono::duration_cast<std::chrono::duration<double>>(endTime - startTime).count();

    long long totalReq = stats.total_requests.load(std::memory_order_relaxed);
    long long totalNs = stats.total_latency_ns.load(std::memory_order_relaxed);

    double qps = (elapsed_s > 0) ? (totalReq / elapsed_s) : 0.0;
    double avg_ms = (totalReq > 0) ? (totalNs / 1e6 / static_cast<double>(totalReq)) : 0.0;

    std::cout << "====== Benchmark Result ======\n";
    std::cout << "Total requests: " << totalReq << "\n";
    std::cout << "Total time:     " << elapsed_s << " s\n";
    std::cout << "QPS:            " << qps << "\n";
    std::cout << "Avg RTT:        " << avg_ms << " ms\n";
    std::cout << "==============================\n";

    return 0;
}