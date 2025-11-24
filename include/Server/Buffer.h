#pragma once

#include <algorithm>
#include <cassert>
#include <cstring>
#include <string>
#include <vector>

// +---------------------------------------------------------+
// |<- prependable ->|<-  readable data  ->|<-  writable   ->|
// |                 |                     |                 |
// 0               readIndex            writeIndex        buffer_.size()

class Buffer {
  public:
    explicit Buffer(size_t initialSize = 4096);

    // 可读/可写字节数
    size_t readableBytes() const;
    size_t writableBytes() const;
    size_t prependableBytes() const;

    const char* peek() const;

    char* beginWrite();
    const char* beginWrite() const;

    // 读取数据
    void retrieve(size_t len);
    void retrieveAll();

    // 以字符串形式读取数据
    std::string retrieveAsString(size_t len);
    std::string retrieveAllAsString();

    // 写入数据
    void append(const void* data, size_t len);
    void append(const std::string& str);

    // 确保有足够的可写空间
    void ensureWritableBytes(size_t len);

    // 更新写入位置
    void hasWritten(size_t len);

    // 收缩缓冲区空间
    void shrinkToFit();

  private:
    // 扩展或整理空间以容纳更多数据
    void makeSpace(size_t len);

  private:
    std::vector<char> buffer_;
    size_t readPos_;
    size_t writePos_;
};