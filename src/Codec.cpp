#include "Codec.h"

LengthHeaderCodec::LengthHeaderCodec(FrameCallback cb) : frameCallback_(std::move(cb)) {}

void LengthHeaderCodec::onMessage(const ConnectionPtr& conn, const std::string& data) {
    std::unique_lock<std::mutex> lock(mutex_);
    auto& buffer = buffers_[conn];
    buffer.append(data);
    processBuffer(conn, buffer);
}

void LengthHeaderCodec::onClose(const ConnectionPtr& conn) {
    std::unique_lock<std::mutex> lock(mutex_);
    buffers_.erase(conn);
}

void LengthHeaderCodec::processBuffer(const ConnectionPtr& conn, std::string& buffer) {
    const size_t headerlen = 4 + 2;  // 4字节长度 + 2字节消息类型

    while (true) {
        if (buffer.size() < headerlen) {
            return;  // 不足以读取头部，等待更多数据
        }

        // 取出 len（不移除，只预读）
        uint32_t len = decodeUint32(buffer.data());
        if (len < 2) {
            // 协议错误：len 至少包括 2字节 msgType
            MetricsRegistry::Instance().totalErrors().inc();
            
            buffer.clear();
            return;  // 非法长度，清空缓存
        }

        uint32_t totalLen = 4 + len;  // 总长度 = 4字节len + len
        if (buffer.size() < totalLen) {
            return;  // 不足以读取完整 frame，等待更多数据
        }

        const char* p = buffer.data();
        p += 4;  // 跳过 len

        uint16_t msgType = decodeUint16(p);
        p += 2;  // 跳过 msgType

        uint32_t bodyLen = len - 2;
        std::string body(p, bodyLen);

        buffer.erase(0, totalLen);  // 移除已处理的数据

        if (frameCallback_) {
            auto start = std::chrono::steady_clock::now();

            try {
                frameCallback_(conn, msgType, body);
                MetricsRegistry::Instance().totalFrames().inc();  // ✅ 真正解出了一帧，再 +1
            } catch (const std::exception& ex) {
                MetricsRegistry::Instance().totalErrors().inc();
                std::cerr << "[Codec] FrameCallback exception: " << ex.what() << "\n";
                // 根据需求决定是否继续 throw，这里建议先不抛，免得上层再统计一遍
                // throw;
            } catch (...) {
                MetricsRegistry::Instance().totalErrors().inc();
                std::cerr << "[Codec] FrameCallback unknown exception\n";
                // throw;
            }

            auto end = std::chrono::steady_clock::now();
            double ms = std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(end - start).count();
            MetricsRegistry::Instance().frameLatency().observe(ms);  // ✅ 统计真正“处理一帧”的耗时
        }
    }
}

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