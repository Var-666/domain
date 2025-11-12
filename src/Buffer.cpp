#include "Buffer.h"

Buffer::Buffer(size_t initialSize) : buffer_(initialSize), readPos_(0), writePos_(0) {}

size_t Buffer::readableBytes() const { return writePos_ - readPos_; }

size_t Buffer::writableBytes() const { return buffer_.size() - writePos_; }

size_t Buffer::prependableBytes() const { return readPos_; }

const char* Buffer::peek() const { return buffer_.data() + readPos_; }

char* Buffer::beginWrite() { return buffer_.data() + writePos_; }

const char* Buffer::beginWrite() const { return buffer_.data() + writePos_; }

void Buffer::retrieve(size_t len) {
    assert(len <= readableBytes());
    if (len < readableBytes()) {
        readPos_ += len;
    } else {
        retrieveAll();
    }
}

void Buffer::retrieveAll() {
    readPos_ = 0;
    writePos_ = 0;
}

std::string Buffer::retrieveAsString(size_t len) {
    assert(len <= readableBytes());
    std::string result(peek(), len);
    retrieve(len);
    return result;
}

std::string Buffer::retrieveAllAsString() { return retrieveAsString(readableBytes()); }

void Buffer::append(const void* data, size_t len) {
    ensureWritableBytes(len);
    std::memcpy(beginWrite(), data, len);
    hasWritten(len);
}

void Buffer::append(const std::string& str) { append(str.data(), str.size()); }

void Buffer::ensureWritableBytes(size_t len) {
    if (writableBytes() < len) {
        makeSpace(len);
    }
}

void Buffer::hasWritten(size_t len) { writePos_ += len; }

void Buffer::shrinkToFit() {
    if (readableBytes() == 0) {
        buffer_.assign(0, 0);
        readPos_ = 0;
        writePos_ = 0;
        return;
    }
    std::vector<char> newBuffer(peek(), peek() + readableBytes());
    buffer_.swap(newBuffer);
    readPos_ = 0;
    writePos_ = buffer_.size();
}

void Buffer::makeSpace(size_t len) {
    if (writableBytes() + prependableBytes() >= len) {
        size_t readable = readableBytes();
        std::memmove(buffer_.data(), peek(), readable);
        readPos_ = 0;
        writePos_ = readPos_ + readable;
    } else {
        size_t newSize = buffer_.size() + std::max(len, buffer_.size());
        buffer_.resize(newSize);
    }
}
