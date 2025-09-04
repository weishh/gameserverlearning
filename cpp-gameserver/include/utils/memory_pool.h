#pragma once

#include <atomic>
#include <memory>
#include <vector>
#include <cstddef>
#include <cassert>
#include <new>

namespace gameserver {

// 固定大小的内存池
template<typename T>
class MemoryPool {
public:
    explicit MemoryPool(size_t chunk_size = 1024)
        : chunk_size_(chunk_size) {
        ExpandPool();
    }
    
    ~MemoryPool() {
        for (auto& chunk : chunks_) {
            ::operator delete(chunk.memory);
        }
    }
    
    // 禁止拷贝
    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;
    
    T* Allocate() {
        Node* head = free_list_.load(std::memory_order_acquire);
        
        while (head) {
            Node* next = head->next;
            if (free_list_.compare_exchange_weak(head, next,
                                                 std::memory_order_release,
                                                 std::memory_order_acquire)) {
                allocated_count_.fetch_add(1, std::memory_order_relaxed);
                return reinterpret_cast<T*>(head);
            }
        }
        
        // 需要扩展内存池
        ExpandPool();
        return Allocate();
    }
    
    void Deallocate(T* ptr) {
        if (!ptr) return;
        
        Node* node = reinterpret_cast<Node*>(ptr);
        Node* head = free_list_.load(std::memory_order_acquire);
        
        do {
            node->next = head;
        } while (!free_list_.compare_exchange_weak(head, node,
                                                   std::memory_order_release,
                                                   std::memory_order_acquire));
        
        allocated_count_.fetch_sub(1, std::memory_order_relaxed);
    }
    
    // 构造对象
    template<typename... Args>
    T* Construct(Args&&... args) {
        T* ptr = Allocate();
        try {
            new (ptr) T(std::forward<Args>(args)...);
        } catch (...) {
            Deallocate(ptr);
            throw;
        }
        return ptr;
    }
    
    // 析构对象
    void Destroy(T* ptr) {
        if (!ptr) return;
        ptr->~T();
        Deallocate(ptr);
    }
    
    size_t GetAllocatedCount() const {
        return allocated_count_.load(std::memory_order_relaxed);
    }
    
    size_t GetPoolSize() const {
        return chunks_.size() * chunk_size_;
    }
    
private:
    union Node {
        Node* next;
        alignas(T) char data[sizeof(T)];
    };
    
    struct Chunk {
        void* memory;
        size_t size;
    };
    
    void ExpandPool() {
        size_t bytes = chunk_size_ * sizeof(Node);
        void* memory = ::operator new(bytes);
        
        Node* nodes = static_cast<Node*>(memory);
        
        // 构建空闲链表
        for (size_t i = 0; i < chunk_size_ - 1; ++i) {
            nodes[i].next = &nodes[i + 1];
        }
        nodes[chunk_size_ - 1].next = nullptr;
        
        // 添加到空闲链表
        Node* head = free_list_.load(std::memory_order_acquire);
        do {
            nodes[chunk_size_ - 1].next = head;
        } while (!free_list_.compare_exchange_weak(head, nodes,
                                                   std::memory_order_release,
                                                   std::memory_order_acquire));
        
        chunks_.push_back({memory, bytes});
    }
    
private:
    std::atomic<Node*> free_list_{nullptr};
    std::atomic<size_t> allocated_count_{0};
    const size_t chunk_size_;
    std::vector<Chunk> chunks_;
};

// 通用对象池
template<typename T>
class ObjectPool {
public:
    using Ptr = std::unique_ptr<T, std::function<void(T*)>>;
    
    explicit ObjectPool(size_t initial_size = 16, size_t max_size = 1024)
        : max_size_(max_size) {
        pool_.reserve(initial_size);
        for (size_t i = 0; i < initial_size; ++i) {
            pool_.emplace_back(std::make_unique<T>());
        }
    }
    
    template<typename... Args>
    Ptr Acquire(Args&&... args) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        T* obj = nullptr;
        
        if (!pool_.empty()) {
            obj = pool_.back().release();
            pool_.pop_back();
            
            // 重新初始化对象
            try {
                new (obj) T(std::forward<Args>(args)...);
            } catch (...) {
                delete obj;
                throw;
            }
        } else {
            obj = new T(std::forward<Args>(args)...);
        }
        
        // 返回带自定义删除器的unique_ptr
        return Ptr(obj, [this](T* ptr) { Release(ptr); });
    }
    
    void Release(T* obj) {
        if (!obj) return;
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (pool_.size() < max_size_) {
            // 重置对象状态
            obj->~T();
            new (obj) T();
            pool_.emplace_back(obj);
        } else {
            delete obj;
        }
    }
    
    size_t GetPoolSize() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return pool_.size();
    }
    
    void Clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        pool_.clear();
    }
    
private:
    mutable std::mutex mutex_;
    std::vector<std::unique_ptr<T>> pool_;
    const size_t max_size_;
};

// 环形缓冲区，用于高性能日志等场景
class RingBuffer {
public:
    explicit RingBuffer(size_t size)
        : size_(size), buffer_(new char[size]), 
          write_pos_(0), read_pos_(0) {}
    
    ~RingBuffer() {
        delete[] buffer_;
    }
    
    bool Write(const void* data, size_t len) {
        if (len > AvailableWrite()) {
            return false;
        }
        
        const char* src = static_cast<const char*>(data);
        size_t write_pos = write_pos_.load(std::memory_order_relaxed);
        size_t to_end = size_ - write_pos;
        
        if (len <= to_end) {
            std::memcpy(buffer_ + write_pos, src, len);
        } else {
            std::memcpy(buffer_ + write_pos, src, to_end);
            std::memcpy(buffer_, src + to_end, len - to_end);
        }
        
        write_pos = (write_pos + len) % size_;
        write_pos_.store(write_pos, std::memory_order_release);
        return true;
    }
    
    bool Read(void* data, size_t len) {
        if (len > AvailableRead()) {
            return false;
        }
        
        char* dst = static_cast<char*>(data);
        size_t read_pos = read_pos_.load(std::memory_order_relaxed);
        size_t to_end = size_ - read_pos;
        
        if (len <= to_end) {
            std::memcpy(dst, buffer_ + read_pos, len);
        } else {
            std::memcpy(dst, buffer_ + read_pos, to_end);
            std::memcpy(dst + to_end, buffer_, len - to_end);
        }
        
        read_pos = (read_pos + len) % size_;
        read_pos_.store(read_pos, std::memory_order_release);
        return true;
    }
    
    size_t AvailableRead() const {
        size_t write_pos = write_pos_.load(std::memory_order_acquire);
        size_t read_pos = read_pos_.load(std::memory_order_acquire);
        
        if (write_pos >= read_pos) {
            return write_pos - read_pos;
        } else {
            return size_ - read_pos + write_pos;
        }
    }
    
    size_t AvailableWrite() const {
        return size_ - AvailableRead() - 1;
    }
    
private:
    const size_t size_;
    char* buffer_;
    std::atomic<size_t> write_pos_;
    std::atomic<size_t> read_pos_;
};

} // namespace gameserver