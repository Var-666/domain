#include <arpa/inet.h>  // htonl/ntohl

#include <algorithm>
#include <atomic>
#include <boost/asio.hpp>
#include <chrono>
#include <cstring>  // std::memcpy
#include <iostream>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

#include "Codec.h"  // 用到 LengthHeaderCodec::encodeFrame

using boost::asio::ip::tcp;

namespace {
    constexpr uint16_t MSG_HEARTBEAT = 1;
    constexpr uint16_t MSG_ECHO = 2;

    // 阻塞读，直到把 len 字节读满
    void readExact(tcp::socket& socket, void* data, std::size_t len) {
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

    struct Frame {
        uint16_t msgType{};
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

    double toMs(std::chrono::steady_clock::duration d) { return std::chrono::duration<double, std::milli>(d).count(); }

}  // namespace

int main(int argc, char** argv) {
    std::string host = "8.159.139.110";
    unsigned short port = 8080;
    std::size_t concurrency = std::thread::hardware_concurrency();  // 并发连接数
    std::size_t totalRequests = 100000;                             // 总请求数（所有线程加起来）

    if (argc >= 2) {
        host = argv[1];
    }
    if (argc >= 3) {
        port = static_cast<unsigned short>(std::stoi(argv[2]));
    }
    if (argc >= 4) {
        concurrency = static_cast<std::size_t>(std::stoul(argv[3]));
    }
    if (argc >= 5) {
        totalRequests = static_cast<std::size_t>(std::stoul(argv[4]));
    }

    std::cout << "[bench] host=" << host << " port=" << port << " concurrency=" << concurrency << " totalRequests=" << totalRequests << "\n";

    std::atomic<std::uint64_t> success{0};
    std::atomic<std::uint64_t> failed{0};

    // 每个线程的延迟记录，单位 ms
    std::vector<std::vector<double>> threadLatencies(concurrency);

    auto startAll = std::chrono::steady_clock::now();

    std::vector<std::thread> threads;
    threads.reserve(concurrency);

    // 平均分配请求数，多出来的前几个线程多 1 个
    std::size_t basePerThread = totalRequests / concurrency;
    std::size_t extra = totalRequests % concurrency;

    for (std::size_t tid = 0; tid < concurrency; ++tid) {
        std::size_t myRequests = basePerThread + (tid < extra ? 1 : 0);

        threads.emplace_back([&, tid, myRequests]() {
            try {
                boost::asio::io_context io;
                tcp::socket socket(io);

                tcp::endpoint ep(boost::asio::ip::make_address(host), port);
                socket.connect(ep);

                // 可选：关闭 Nagle，减小延迟抖动
                boost::asio::ip::tcp::no_delay nd(true);
                socket.set_option(nd);

                // 先发一个心跳试试
                sendFrame(socket, MSG_HEARTBEAT, "");

                std::string payload = "hello router";

                auto& latVec = threadLatencies[tid];
                latVec.reserve(myRequests);

                for (std::size_t i = 0; i < myRequests; ++i) {
                    auto t0 = std::chrono::steady_clock::now();

                    // 发送 echo
                    sendFrame(socket, MSG_ECHO, payload);

                    // 同步等待响应
                    Frame resp = readFrame(socket);

                    auto t1 = std::chrono::steady_clock::now();
                    double ms = toMs(t1 - t0);
                    latVec.push_back(ms);

                    if (resp.msgType != MSG_ECHO) {
                        ++failed;
                        // 可以选择中断，或者继续
                        // break;
                    } else {
                        ++success;
                    }
                }

                socket.close();
            } catch (const std::exception& ex) {
                std::cerr << "[thread " << tid << "] exception: " << ex.what() << "\n";
                // 把没完成的请求都算失败
                failed.fetch_add(myRequests, std::memory_order_relaxed);
            }
        });
    }

    for (auto& th : threads) {
        th.join();
    }

    auto endAll = std::chrono::steady_clock::now();
    double totalSec = std::chrono::duration<double>(endAll - startAll).count();

    std::uint64_t succ = success.load();
    std::uint64_t fail = failed.load();
    std::uint64_t done = succ + fail;

    // 汇总所有线程的延迟
    std::vector<double> allLat;
    allLat.reserve(done);
    for (const auto& v : threadLatencies) {
        allLat.insert(allLat.end(), v.begin(), v.end());
    }

    std::sort(allLat.begin(), allLat.end());

    auto percentile = [&](double p) -> double {
        if (allLat.empty())
            return 0.0;
        double idx = p * (allLat.size() - 1);
        return allLat[static_cast<std::size_t>(idx)];
    };

    double avg = 0.0;
    if (!allLat.empty()) {
        avg = std::accumulate(allLat.begin(), allLat.end(), 0.0) / allLat.size();
    }

    double p95 = percentile(0.95);
    double p99 = percentile(0.99);

    double qps = (totalSec > 0) ? (succ / totalSec) : 0.0;

    std::cout << "===== Benchmark Result =====\n";
    std::cout << "concurrency      : " << concurrency << "\n";
    std::cout << "total requests   : " << totalRequests << "\n";
    std::cout << "done (succ+fail) : " << done << " (succ=" << succ << ", fail=" << fail << ")\n";
    std::cout << "total time (s)   : " << totalSec << "\n";
    std::cout << "QPS              : " << qps << "\n";
    std::cout << "avg latency (ms) : " << avg << "\n";
    std::cout << "p95 latency (ms) : " << p95 << "\n";
    std::cout << "p99 latency (ms) : " << p99 << "\n";
    std::cout << "============================\n";

    return 0;
}