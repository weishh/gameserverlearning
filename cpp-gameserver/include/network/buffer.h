#pragma once

#include <vector>
#include <string>
#include <algorithm>
#include <cassert>
#include <cstring>

namespace gameserver {

/// 一个自增长的缓冲区类，支持高效的前后添加数据
/// 内部布局: [prepend_bytes][readable_bytes][writable_bytes]
///           |              |               |
///        begin()      reader_index_   writer_index_
class Buffer {
public:
    static constexpr size_t kCheapPrepend = 8;
    static constexpr size_t kInitialSize = 1024;
    
    explicit Buffer(size_t initial_size = kInitialSize)
        : buffer_(kCheapPrepend + initial_size),
          reader_index_(kCheapPrepend),
          writer_index_(kCheapPrepend) {
        assert(ReadableBytes() == 0);
        assert(WritableBytes() == initial_size);
        assert(PrependableBytes() == kCheapPrepend);
    }
    
    // 可读字节数
    size_t ReadableBytes() const { 
        return writer_index_ - reader_index_; 
    }
    
    // 可写字节数
    size_t WritableBytes() const { 
        return buffer_.size() - writer_index_; 
    }
    
    // 前置可写字节数
    size_t PrependableBytes() const { 
        return reader_index_; 
    }
    
    // 返回可读数据的起始地址
    const char* Peek() const { 
        return Begin() + reader_index_; 
    }
    
    // 查找\r\n
    const char* FindCRLF() const {
        const char* crlf = std::search(Peek(), BeginWrite(), kCRLF, kCRLF + 2);
        return crlf == BeginWrite() ? nullptr : crlf;
    }
    
    // 查找\n
    const char* FindEOL() const {
        const void* eol = memchr(Peek(), '\n', ReadableBytes());
        return static_cast<const char*>(eol);
    }
    
    // 取回数据
    void Retrieve(size_t len) {
        assert(len <= ReadableBytes());
        if (len < ReadableBytes()) {
            reader_index_ += len;
        } else {
            RetrieveAll();
        }
    }
    
    // 取回到指定位置
    void RetrieveUntil(const char* end) {
        assert(Peek() <= end);
        assert(end <= BeginWrite());
        Retrieve(end - Peek());
    }
    
    // 取回所有数据
    void RetrieveAll() {
        reader_index_ = kCheapPrepend;
        writer_index_ = kCheapPrepend;
    }
    
    // 取回所有数据作为string
    std::string RetrieveAllAsString() {
        return RetrieveAsString(ReadableBytes());
    }
    
    // 取回指定长度数据作为string
    std::string RetrieveAsString(size_t len) {
        assert(len <= ReadableBytes());
        std::string result(Peek(), len);
        Retrieve(len);
        return result;
    }
    
    // 添加数据
    void Append(const char* data, size_t len) {
        EnsureWritableBytes(len);
        std::copy(data, data + len, BeginWrite());
        HasWritten(len);
    }
    
    void Append(const void* data, size_t len) {
        Append(static_cast<const char*>(data), len);
    }
    
    void Append(const std::string& str) {
        Append(str.data(), str.size());
    }
    
    // 确保有足够的写空间
    void EnsureWritableBytes(size_t len) {
        if (WritableBytes() < len) {
            MakeSpace(len);
        }
        assert(WritableBytes() >= len);
    }
    
    // 返回可写区域的起始地址
    char* BeginWrite() { 
        return Begin() + writer_index_; 
    }
    
    const char* BeginWrite() const { 
        return Begin() + writer_index_; 
    }
    
    // 写入数据后更新写索引
    void HasWritten(size_t len) {
        assert(len <= WritableBytes());
        writer_index_ += len;
    }
    
    // 撤销写入
    void Unwrite(size_t len) {
        assert(len <= ReadableBytes());
        writer_index_ -= len;
    }
    
    // 写入整数（网络字节序）
    void AppendInt64(int64_t x);
    void AppendInt32(int32_t x);
    void AppendInt16(int16_t x);
    void AppendInt8(int8_t x);
    
    // 读取整数（网络字节序）
    int64_t ReadInt64();
    int32_t ReadInt32();
    int16_t ReadInt16();
    int8_t ReadInt8();
    
    // 查看整数但不移动读索引
    int64_t PeekInt64() const;
    int32_t PeekInt32() const;
    int16_t PeekInt16() const;
    int8_t PeekInt8() const;
    
    // 前置写入
    void Prepend(const void* data, size_t len) {
        assert(len <= PrependableBytes());
        reader_index_ -= len;
        const char* d = static_cast<const char*>(data);
        std::copy(d, d + len, Begin() + reader_index_);
    }
    
    // 收缩空间
    void Shrink(size_t reserve) {
        Buffer other;
        other.EnsureWritableBytes(ReadableBytes() + reserve);
        other.Append(Peek(), ReadableBytes());
        Swap(other);
    }
    
    // 容量
    size_t Capacity() const {
        return buffer_.capacity();
    }
    
    // 从socket读取数据
    ssize_t ReadFromSocket(int fd, int* saved_errno);
    
    // 交换
    void Swap(Buffer& rhs) {
        buffer_.swap(rhs.buffer_);
        std::swap(reader_index_, rhs.reader_index_);
        std::swap(writer_index_, rhs.writer_index_);
    }
    
private:
    char* Begin() { 
        return buffer_.data(); 
    }
    
    const char* Begin() const { 
        return buffer_.data(); 
    }
    
    void MakeSpace(size_t len) {
        if (WritableBytes() + PrependableBytes() < len + kCheapPrepend) {
            // 需要扩容
            buffer_.resize(writer_index_ + len);
        } else {
            // 移动数据到前面
            assert(kCheapPrepend < reader_index_);
            size_t readable = ReadableBytes();
            std::copy(Begin() + reader_index_,
                     Begin() + writer_index_,
                     Begin() + kCheapPrepend);
            reader_index_ = kCheapPrepend;
            writer_index_ = reader_index_ + readable;
            assert(readable == ReadableBytes());
        }
    }
    
private:
    std::vector<char> buffer_;
    size_t reader_index_;
    size_t writer_index_;
    
    static const char kCRLF[];
};

} // namespace gameserver