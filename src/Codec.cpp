#include "Codec.h"

#include <spdlog/spdlog.h>

LengthHeaderCodec::LengthHeaderCodec(FrameCallback cb) : frameCallback_(std::move(cb)) {}

void LengthHeaderCodec::onMessage(const ConnectionPtr& conn, Buffer& buf) {
    constexpr std::size_t headerlen = 4 + 2;
    while (true) {
        // 1. 先看头是否完整
        if (buf.readableBytes() < headerlen) {
            break;
        }
        const char* p = buf.peek();

        // 2. 读取 len（不移动读指针）
        std::uint32_t len = decodeUint32(p);
        if (len < 2) {
            MetricsRegistry::Instance().totalErrors().inc();
            SPDLOG_ERROR("[Codec] Invalid frame length:{}, drop all remaining bytes ", len);
            buf.retrieveAll();
            break;
        }

        std::uint32_t totalLen = 4 + len;
        if (buf.readableBytes() < totalLen) {
            // 一个完整 frame 还没到齐，退出等待下次
            break;
        }

        // 3. 真正开始消费数据：先跳过 4 字节 length
        buf.retrieve(4);

        // 4. 读取 msgType
        if (buf.readableBytes() < 2) {
            // 理论上不会发生，因为上面已经检查 totalLen 充足
            buf.retrieveAll();
            break;
        }

        std::uint16_t msgType = decodeUint16(buf.peek());
        buf.retrieve(2);

        // 5. 读取 body
        std::uint32_t bodyLen = len - 2;
        std::string body;
        body.resize(bodyLen);
        if (bodyLen > 0) {
            if (buf.readableBytes() < bodyLen) {
                // 理论上也不会发生（已经保证 totalLen 够）
                buf.retrieveAll();
                break;
            }
            std::memcpy(body.data(), buf.peek(), bodyLen);
            buf.retrieve(bodyLen);
        }

        // 6. 调用上层回调 + 统计 Metrics（真正成功解出了一帧）
        if (frameCallback_) {
            auto start = std::chrono::steady_clock::now();

            try {
                frameCallback_(conn, msgType, body);
                MetricsRegistry::Instance().totalFrames().inc();
            } catch (const std::exception& ex) {
                MetricsRegistry::Instance().totalErrors().inc();
                std::cerr << "[Codec] FrameCallback exception: " << ex.what() << "\n";
                // 这里不再往上抛，避免整个 worker 线程被异常干掉
            } catch (...) {
                MetricsRegistry::Instance().totalErrors().inc();
                std::cerr << "[Codec] FrameCallback unknown exception\n";
            }

            auto end = std::chrono::steady_clock::now();
            double ms = std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(end - start).count();
            MetricsRegistry::Instance().frameLatency().observe(ms);
        }
        // 7. while(true) 继续尝试解析下一帧（如果 Buffer 中还有完整数据）
    }
}

void LengthHeaderCodec::onClose(const ConnectionPtr& conn) {}

void LengthHeaderCodec::send(const ConnectionPtr& conn, uint16_t msgType, const std::string& body) {
    std::string buf = encodeFrame(msgType, body);
    conn->send(buf);
}

std::string LengthHeaderCodec::encodeFrame(uint16_t msgType, const std::string& body) {
    uint32_t len = 2 + static_cast<uint32_t>(body.size());
    std::string buf;
    buf.resize(4 + 2 + body.size());

    char* p = &buf[0];
    encodeUint32(p, len);
    p += 4;
    encodeUint16(p, msgType);
    p += 2;
    if (!body.empty()) {
        std::memcpy(p, body.data(), body.size());
    }
    return buf;
}

uint32_t LengthHeaderCodec::decodeUint32(const char* p) {
    uint32_t v;
    std::memcpy(&v, p, sizeof(uint32_t));
    return ntohl(v);
}

uint16_t LengthHeaderCodec::decodeUint16(const char* p) {
    uint16_t v;
    std::memcpy(&v, p, sizeof(uint16_t));
    return ntohs(v);
}

void LengthHeaderCodec::encodeUint32(char* p, uint32_t v) {
    uint32_t netV = htonl(v);
    std::memcpy(p, &netV, sizeof(uint32_t));
}

void LengthHeaderCodec::encodeUint16(char* p, uint16_t v) {
    uint16_t netV = htons(v);
    std::memcpy(p, &netV, sizeof(uint16_t));
}