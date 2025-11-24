#include <arpa/inet.h>  // htonl/ntohl

#include <algorithm>
#include <atomic>
#include <google/protobuf/empty.pb.h>
#include <boost/asio.hpp>
#include <chrono>
#include <cstring>  // std::memcpy
#include <iostream>
#include <nlohmann/json.hpp>
#include <numeric>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "Codec.h"  // 用到 LengthHeaderCodec::encodeFrame
#include "Routes/CoreRoutes.h"

using boost::asio::ip::tcp;

namespace {
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

struct Options {
    std::string host = "127.0.0.1";
    unsigned short port = 8080;
    std::size_t concurrency = std::thread::hardware_concurrency();
    std::size_t totalRequests = 100000;
    std::size_t payloadSize = 12;  // 默认 payload 大小
    std::vector<uint16_t> errorMsgTypes = {0xFFFF, 65000, 65001, 65002, 65003};  // 识别为错误帧的 msgType
    bool sendHeartbeat = true;
    std::string mode = "raw";  // raw/json/proto
};

Options parseOptions(int argc, char** argv) {
    Options opt;

    // 兼容原有 positional 参数：host port concurrency requests
    std::size_t positional = 0;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.rfind("--", 0) == 0) {
            if (arg == "--payload" && i + 1 < argc) {
                opt.payloadSize = static_cast<std::size_t>(std::stoul(argv[++i]));
            } else if (arg == "--error-type" && i + 1 < argc) {
                opt.errorMsgTypes.clear();
                opt.errorMsgTypes.push_back(static_cast<std::uint16_t>(std::stoul(argv[++i])));
            } else if (arg == "--no-heartbeat") {
                opt.sendHeartbeat = false;
            } else if (arg == "--mode" && i + 1 < argc) {
                opt.mode = argv[++i];
            }
            continue;
        }

        switch (positional) {
            case 0:
                opt.host = arg;
                break;
            case 1:
                opt.port = static_cast<unsigned short>(std::stoi(arg));
                break;
            case 2:
                opt.concurrency = static_cast<std::size_t>(std::stoul(arg));
                break;
            case 3:
                opt.totalRequests = static_cast<std::size_t>(std::stoul(arg));
                break;
            default:
                break;
        }
        ++positional;
    }

    if (opt.concurrency == 0) {
        opt.concurrency = std::thread::hardware_concurrency();
    }
    return opt;
}

int main(int argc, char** argv) {
    Options opt = parseOptions(argc, argv);

    std::cout << "[bench] host=" << opt.host << " port=" << opt.port << " concurrency=" << opt.concurrency << " totalRequests=" << opt.totalRequests
              << " payload=" << opt.payloadSize << " heartbeat=" << (opt.sendHeartbeat ? "on" : "off") << " mode=" << opt.mode << "\n";

    std::atomic<std::uint64_t> success{0};
    std::atomic<std::uint64_t> failed{0};
    std::atomic<std::uint64_t> dropped{0};  // 收到错误帧

    // 每个线程的延迟记录，单位 ms
    std::vector<std::vector<double>> threadLatencies(opt.concurrency);
    std::vector<std::unordered_map<uint16_t, std::uint64_t>> threadErrorTypes(opt.concurrency);

    auto startAll = std::chrono::steady_clock::now();

    std::vector<std::thread> threads;
    threads.reserve(opt.concurrency);

    // 平均分配请求数，多出来的前几个线程多 1 个
    std::size_t basePerThread = opt.totalRequests / opt.concurrency;
    std::size_t extra = opt.totalRequests % opt.concurrency;

    // 准备 payload
    std::string payload;
    payload.resize(opt.payloadSize, 'x');

    for (std::size_t tid = 0; tid < opt.concurrency; ++tid) {
        std::size_t myRequests = basePerThread + (tid < extra ? 1 : 0);

        threads.emplace_back([&, tid, myRequests]() {
            try {
                boost::asio::io_context io;
                tcp::socket socket(io);

                tcp::endpoint ep(boost::asio::ip::make_address(opt.host), opt.port);
                socket.connect(ep);

                // 可选：关闭 Nagle，减小延迟抖动
                boost::asio::ip::tcp::no_delay nd(true);
                socket.set_option(nd);

                // 先发一个心跳试试（可选）
                if (opt.sendHeartbeat) {
                    sendFrame(socket, MSG_HEARTBEAT, "");
                }

                auto& latVec = threadLatencies[tid];
                latVec.reserve(myRequests);

                for (std::size_t i = 0; i < myRequests; ++i) {
                    auto t0 = std::chrono::steady_clock::now();

                    uint16_t msgTypeToSend = MSG_ECHO;
                    std::string body = payload;
                    if (opt.mode == "json") {
                        msgTypeToSend = MSG_JSON_ECHO;
                        nlohmann::json j;
                        j["msg"] = payload;
                        body = j.dump();
                    } else if (opt.mode == "proto") {
                        msgTypeToSend = MSG_PROTO_PING;
                        google::protobuf::Empty empty;
                        body = empty.SerializeAsString();
                    }

                    sendFrame(socket, msgTypeToSend, body);

                    // 同步等待响应
                    Frame resp = readFrame(socket);

                    auto t1 = std::chrono::steady_clock::now();
                    double ms = toMs(t1 - t0);
                    if (resp.msgType == msgTypeToSend) {
                        latVec.push_back(ms);
                        ++success;
                    } else if (std::find(opt.errorMsgTypes.begin(), opt.errorMsgTypes.end(), resp.msgType) != opt.errorMsgTypes.end()) {
                        ++dropped;
                        threadErrorTypes[tid][resp.msgType]++;
                    } else {
                        ++failed;
                        threadErrorTypes[tid][resp.msgType]++;
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
    std::uint64_t drop = dropped.load();
    std::uint64_t done = succ + fail + drop;

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

    // 合并其他响应类型计数
    std::unordered_map<uint16_t, std::uint64_t> otherTypes;
    for (const auto& mp : threadErrorTypes) {
        for (const auto& kv : mp) {
            otherTypes[kv.first] += kv.second;
        }
    }

    std::cout << "===== Benchmark Result =====\n";
    std::cout << "concurrency      : " << opt.concurrency << "\n";
    std::cout << "total requests   : " << opt.totalRequests << "\n";
    std::cout << "done (succ+fail+drop) : " << done << " (succ=" << succ << ", fail=" << fail << ", drop=" << drop << ")\n";
    std::cout << "total time (s)   : " << totalSec << "\n";
    std::cout << "QPS              : " << qps << "\n";
    std::cout << "avg latency (ms) : " << avg << "\n";
    std::cout << "p95 latency (ms) : " << p95 << "\n";
    std::cout << "p99 latency (ms) : " << p99 << "\n";
    if (!otherTypes.empty()) {
        std::cout << "other resp types :";
        for (auto& kv : otherTypes) {
            std::cout << " [" << kv.first << "]=" << kv.second;
        }
        std::cout << "\n";
    }
    std::cout << "============================\n";

    return 0;
}
