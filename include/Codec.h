#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>

#include "AsioConnection.h"

// 长度头 + 消息类型的简单协议：
// [4字节len][2字节msgType][Body...]
// len = 2 + body.size()
class LengthHeaderCodec {
  public:
    using FrameCallback = std::function<void(const ConnectionPtr&, uint16_t /*msgType*/, const std::string& /*body*/)>;

    explicit LengthHeaderCodec(FrameCallback cb);

    // 接收原始数据（AsioConnection onMessage 里调用）
    void onMessage(const ConnectionPtr& conn, const std::string& data);

    // 连接关闭（AsioServer onClose 里调用），清理对应缓存
    void onClose(const ConnectionPtr& conn);

    // 发送一个 frame（静态函数，只负责编码 + 调用 conn->send）
    static void send(const ConnectionPtr& conn, uint16_t msgType, const std::string& body);

    static std::string encodeFrame(uint16_t msgType, const std::string& body);

  private:
    // 内部使用：从缓存中解析完整 frame
    void processBuffer(const ConnectionPtr& conn, std::string& buffer);

    // 编码/解码辅助函数
    static uint32_t decodeUint32(const char* p);
    static uint16_t decodeUint16(const char* p);
    static void encodeUint32(char* p, uint32_t v);
    static void encodeUint16(char* p, uint16_t v);

  private:
    FrameCallback frameCallback_;

    std::unordered_map<ConnectionPtr, std::string> buffers_;
    std::mutex mutex_;
};