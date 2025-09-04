# 模块 A: C++游戏服务端基础（Week 1-2）

## 📚 第一周：现代C++与高性能网络编程

### 1. 现代C++核心特性在游戏服务器中的应用

#### RAII与智能指针
```cpp
// 游戏服务器中的连接管理
class ConnectionManager {
    // 使用智能指针自动管理生命周期
    std::unordered_map<int, std::shared_ptr<Connection>> connections_;
    
public:
    void AddConnection(int fd) {
        // 自动管理内存，异常安全
        auto conn = std::make_shared<Connection>(fd);
        connections_[fd] = conn;
    }
    
    void RemoveConnection(int fd) {
        connections_.erase(fd);  // 自动释放资源
    }
};

// RAII封装的Socket类
class Socket {
    int fd_;
public:
    explicit Socket(int fd) : fd_(fd) {}
    ~Socket() { 
        if (fd_ >= 0) ::close(fd_); 
    }
    
    // 禁止拷贝，支持移动
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    
    Socket(Socket&& other) noexcept : fd_(other.fd_) {
        other.fd_ = -1;
    }
    
    Socket& operator=(Socket&& other) noexcept {
        if (this != &other) {
            if (fd_ >= 0) ::close(fd_);
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }
};
```

#### Move语义优化性能
```cpp
// 消息传递优化
class Message {
    std::vector<uint8_t> data_;
public:
    // 移动构造，避免深拷贝
    Message(Message&& other) noexcept 
        : data_(std::move(other.data_)) {}
    
    // 完美转发
    template<typename T>
    void SetPayload(T&& payload) {
        data_ = std::forward<T>(payload);
    }
};

// 高性能消息队列
template<typename T>
class MessageQueue {
    std::queue<T> queue_;
    mutable std::mutex mutex_;
    
public:
    // 使用移动语义避免拷贝
    void Push(T&& msg) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(std::move(msg));
    }
    
    bool TryPop(T& msg) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return false;
        msg = std::move(queue_.front());
        queue_.pop();
        return true;
    }
};
```

### 2. Linux高性能网络编程：epoll实战

#### Epoll封装
```cpp
#include <sys/epoll.h>
#include <vector>
#include <unordered_map>

class EpollPoller {
    static constexpr int kInitEventListSize = 16;
    
    int epoll_fd_;
    std::vector<struct epoll_event> events_;
    std::unordered_map<int, Channel*> channels_;
    
public:
    EpollPoller() 
        : epoll_fd_(::epoll_create1(EPOLL_CLOEXEC)),
          events_(kInitEventListSize) {
        if (epoll_fd_ < 0) {
            throw std::runtime_error("epoll_create1 failed");
        }
    }
    
    ~EpollPoller() {
        ::close(epoll_fd_);
    }
    
    // 添加/修改/删除监听
    void UpdateChannel(Channel* channel) {
        int fd = channel->GetFd();
        struct epoll_event event;
        event.events = channel->GetEvents();
        event.data.ptr = channel;
        
        if (channels_.find(fd) != channels_.end()) {
            // 修改已存在的
            ::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &event);
        } else {
            // 添加新的
            channels_[fd] = channel;
            ::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &event);
        }
    }
    
    void RemoveChannel(Channel* channel) {
        int fd = channel->GetFd();
        channels_.erase(fd);
        ::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
    }
    
    // 等待事件
    std::vector<Channel*> Poll(int timeout_ms) {
        int num_events = ::epoll_wait(epoll_fd_, 
                                      events_.data(), 
                                      events_.size(), 
                                      timeout_ms);
        
        std::vector<Channel*> active_channels;
        
        if (num_events > 0) {
            active_channels.reserve(num_events);
            
            for (int i = 0; i < num_events; ++i) {
                Channel* channel = static_cast<Channel*>(events_[i].data.ptr);
                channel->SetRevents(events_[i].events);
                active_channels.push_back(channel);
            }
            
            // 动态扩容
            if (num_events == static_cast<int>(events_.size())) {
                events_.resize(events_.size() * 2);
            }
        }
        
        return active_channels;
    }
};
```

#### Channel事件分发
```cpp
class Channel {
    EventLoop* loop_;
    const int fd_;
    int events_;     // 关心的事件
    int revents_;    // 实际发生的事件
    
    using EventCallback = std::function<void()>;
    EventCallback read_callback_;
    EventCallback write_callback_;
    EventCallback error_callback_;
    EventCallback close_callback_;
    
public:
    Channel(EventLoop* loop, int fd) 
        : loop_(loop), fd_(fd), events_(0), revents_(0) {}
    
    // 处理事件
    void HandleEvent() {
        if (revents_ & EPOLLERR) {
            if (error_callback_) error_callback_();
        }
        
        if (revents_ & (EPOLLIN | EPOLLPRI | EPOLLRDHUP)) {
            if (read_callback_) read_callback_();
        }
        
        if (revents_ & EPOLLOUT) {
            if (write_callback_) write_callback_();
        }
        
        if (revents_ & EPOLLHUP && !(revents_ & EPOLLIN)) {
            if (close_callback_) close_callback_();
        }
    }
    
    // 设置关心的事件
    void EnableReading() {
        events_ |= (EPOLLIN | EPOLLPRI);
        Update();
    }
    
    void EnableWriting() {
        events_ |= EPOLLOUT;
        Update();
    }
    
    void DisableWriting() {
        events_ &= ~EPOLLOUT;
        Update();
    }
    
    void DisableAll() {
        events_ = 0;
        Update();
    }
    
private:
    void Update() {
        loop_->UpdateChannel(this);
    }
};
```

### 3. 高性能内存管理

#### Lock-free内存池
```cpp
template<typename T>
class LockFreePool {
    struct Node {
        std::atomic<Node*> next;
        alignas(T) char data[sizeof(T)];
    };
    
    std::atomic<Node*> head_{nullptr};
    std::atomic<size_t> size_{0};
    
public:
    T* Allocate() {
        Node* head = head_.load(std::memory_order_acquire);
        
        while (head) {
            Node* next = head->next.load(std::memory_order_relaxed);
            
            // CAS操作
            if (head_.compare_exchange_weak(head, next,
                                           std::memory_order_release,
                                           std::memory_order_acquire)) {
                size_.fetch_sub(1, std::memory_order_relaxed);
                return reinterpret_cast<T*>(&head->data);
            }
        }
        
        // 池为空，分配新内存
        return reinterpret_cast<T*>(new Node());
    }
    
    void Deallocate(T* ptr) {
        if (!ptr) return;
        
        Node* node = reinterpret_cast<Node*>(
            reinterpret_cast<char*>(ptr) - offsetof(Node, data)
        );
        
        Node* head = head_.load(std::memory_order_relaxed);
        
        do {
            node->next.store(head, std::memory_order_relaxed);
        } while (!head_.compare_exchange_weak(head, node,
                                             std::memory_order_release,
                                             std::memory_order_acquire));
        
        size_.fetch_add(1, std::memory_order_relaxed);
    }
    
    size_t Size() const {
        return size_.load(std::memory_order_relaxed);
    }
};
```

#### 缓存友好的数据结构
```cpp
// 避免False Sharing
struct alignas(64) CacheLinePadded {
    std::atomic<int64_t> value;
    char padding[64 - sizeof(std::atomic<int64_t>)];
};

// 数据局部性优化的玩家数据
class Player {
    // 热数据放在一起
    struct HotData {
        int32_t id;
        float x, y, z;           // 位置
        float vx, vy, vz;        // 速度
        int32_t hp;
        int32_t state;
        int64_t last_update;
    } __attribute__((packed));
    
    // 冷数据分开存储
    struct ColdData {
        std::string name;
        std::vector<int> items;
        // ... 其他不常访问的数据
    };
    
    HotData hot_;
    std::unique_ptr<ColdData> cold_;
    
public:
    // 频繁调用的更新函数只访问热数据
    void Update(int64_t now) {
        hot_.x += hot_.vx;
        hot_.y += hot_.vy;
        hot_.z += hot_.vz;
        hot_.last_update = now;
    }
};
```

## 📚 第二周：游戏服务器架构实战

### 1. Reactor模式实现

```cpp
class EventLoop {
    std::atomic<bool> quit_{false};
    std::unique_ptr<EpollPoller> poller_;
    std::thread::id thread_id_;
    
    // 待执行的回调
    std::mutex mutex_;
    std::vector<std::function<void()>> pending_functors_;
    
    // 用于唤醒的eventfd
    int wakeup_fd_;
    std::unique_ptr<Channel> wakeup_channel_;
    
public:
    EventLoop() 
        : thread_id_(std::this_thread::get_id()),
          poller_(std::make_unique<EpollPoller>()),
          wakeup_fd_(::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC)) {
        
        wakeup_channel_ = std::make_unique<Channel>(this, wakeup_fd_);
        wakeup_channel_->SetReadCallback([this] { HandleWakeup(); });
        wakeup_channel_->EnableReading();
    }
    
    void Loop() {
        while (!quit_) {
            auto active_channels = poller_->Poll(1000);
            
            for (auto* channel : active_channels) {
                channel->HandleEvent();
            }
            
            DoPendingFunctors();
        }
    }
    
    void RunInLoop(std::function<void()> cb) {
        if (IsInLoopThread()) {
            cb();
        } else {
            QueueInLoop(std::move(cb));
        }
    }
    
    void QueueInLoop(std::function<void()> cb) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pending_functors_.push_back(std::move(cb));
        }
        
        if (!IsInLoopThread()) {
            Wakeup();
        }
    }
    
private:
    void DoPendingFunctors() {
        std::vector<std::function<void()>> functors;
        
        {
            std::lock_guard<std::mutex> lock(mutex_);
            functors.swap(pending_functors_);
        }
        
        for (const auto& func : functors) {
            func();
        }
    }
    
    void Wakeup() {
        uint64_t one = 1;
        ::write(wakeup_fd_, &one, sizeof(one));
    }
    
    void HandleWakeup() {
        uint64_t one;
        ::read(wakeup_fd_, &one, sizeof(one));
    }
    
    bool IsInLoopThread() const {
        return thread_id_ == std::this_thread::get_id();
    }
};
```

### 2. 高性能TCP服务器

```cpp
class TcpServer {
    EventLoop* loop_;
    std::unique_ptr<Acceptor> acceptor_;
    std::shared_ptr<EventLoopThreadPool> thread_pool_;
    
    using ConnectionMap = std::unordered_map<std::string, TcpConnectionPtr>;
    ConnectionMap connections_;
    
    // 回调
    ConnectionCallback connection_callback_;
    MessageCallback message_callback_;
    
    std::atomic<int> next_conn_id_{1};
    
public:
    TcpServer(EventLoop* loop, const InetAddress& listen_addr)
        : loop_(loop),
          acceptor_(std::make_unique<Acceptor>(loop, listen_addr)),
          thread_pool_(std::make_shared<EventLoopThreadPool>(loop)) {
        
        acceptor_->SetNewConnectionCallback(
            [this](int sockfd, const InetAddress& peer_addr) {
                NewConnection(sockfd, peer_addr);
            });
    }
    
    void Start() {
        thread_pool_->Start();
        loop_->RunInLoop([this] { acceptor_->Listen(); });
    }
    
    void SetThreadNum(int num_threads) {
        thread_pool_->SetThreadNum(num_threads);
    }
    
private:
    void NewConnection(int sockfd, const InetAddress& peer_addr) {
        // Round-robin分配到IO线程
        EventLoop* io_loop = thread_pool_->GetNextLoop();
        
        char buf[32];
        snprintf(buf, sizeof(buf), "#%d", next_conn_id_++);
        std::string conn_name = buf;
        
        auto conn = std::make_shared<TcpConnection>(
            io_loop, conn_name, sockfd, local_addr, peer_addr);
        
        connections_[conn_name] = conn;
        
        conn->SetConnectionCallback(connection_callback_);
        conn->SetMessageCallback(message_callback_);
        conn->SetCloseCallback(
            [this](const TcpConnectionPtr& conn) {
                RemoveConnection(conn);
            });
        
        io_loop->RunInLoop([conn] { conn->ConnectEstablished(); });
    }
    
    void RemoveConnection(const TcpConnectionPtr& conn) {
        loop_->RunInLoop([this, conn] {
            connections_.erase(conn->GetName());
            conn->GetLoop()->QueueInLoop(
                [conn] { conn->ConnectDestroyed(); });
        });
    }
};
```

### 3. 消息协议设计

```cpp
// 协议头
struct MessageHeader {
    uint32_t magic;      // 魔数，用于校验
    uint32_t length;     // 消息体长度
    uint16_t type;       // 消息类型
    uint16_t flags;      // 标志位
    uint32_t sequence;   // 序列号
} __attribute__((packed));

// 消息编解码器
class Codec {
    static constexpr uint32_t kMagicNumber = 0x12345678;
    static constexpr size_t kHeaderSize = sizeof(MessageHeader);
    static constexpr size_t kMaxMessageSize = 65536;
    
public:
    // 编码消息
    static Buffer Encode(uint16_t type, const google::protobuf::Message& message) {
        Buffer buf;
        
        // 预留头部空间
        buf.EnsureWritableBytes(kHeaderSize);
        buf.HasWritten(kHeaderSize);
        
        // 序列化消息体
        std::string body = message.SerializeAsString();
        buf.Append(body);
        
        // 填充头部
        MessageHeader header;
        header.magic = htonl(kMagicNumber);
        header.length = htonl(body.size());
        header.type = htons(type);
        header.flags = 0;
        header.sequence = 0;
        
        // 写入头部
        buf.Prepend(&header, sizeof(header));
        
        return buf;
    }
    
    // 解码消息
    static bool Decode(Buffer* buf, MessageCallback callback) {
        while (buf->ReadableBytes() >= kHeaderSize) {
            // 检查魔数
            uint32_t magic = buf->PeekInt32();
            if (magic != kMagicNumber) {
                // 协议错误，关闭连接
                return false;
            }
            
            // 读取消息长度
            const char* data = buf->Peek();
            const MessageHeader* header = 
                reinterpret_cast<const MessageHeader*>(data);
            
            uint32_t msg_len = ntohl(header->length);
            
            if (msg_len > kMaxMessageSize) {
                // 消息过大
                return false;
            }
            
            if (buf->ReadableBytes() < kHeaderSize + msg_len) {
                // 数据不完整，等待更多数据
                break;
            }
            
            // 读取完整消息
            buf->Retrieve(kHeaderSize);
            std::string body = buf->RetrieveAsString(msg_len);
            
            // 回调处理消息
            callback(ntohs(header->type), body);
        }
        
        return true;
    }
};
```

## 🎮 实战项目：高性能游戏服务器框架

### 完整的Echo服务器实现

```cpp
#include <iostream>
#include <signal.h>
#include "TcpServer.h"
#include "EventLoop.h"
#include "InetAddress.h"

class EchoServer {
    EventLoop* loop_;
    TcpServer server_;
    
    // 统计信息
    std::atomic<int64_t> total_connections_{0};
    std::atomic<int64_t> current_connections_{0};
    std::atomic<int64_t> total_messages_{0};
    std::atomic<int64_t> total_bytes_{0};
    
public:
    EchoServer(EventLoop* loop, const InetAddress& addr)
        : loop_(loop), server_(loop, addr) {
        
        server_.SetConnectionCallback(
            [this](const TcpConnectionPtr& conn) {
                OnConnection(conn);
            });
        
        server_.SetMessageCallback(
            [this](const TcpConnectionPtr& conn, Buffer* buf, Timestamp) {
                OnMessage(conn, buf);
            });
        
        // 使用多线程
        server_.SetThreadNum(std::thread::hardware_concurrency());
    }
    
    void Start() {
        server_.Start();
        
        // 定期打印统计
        loop_->RunEvery(10.0, [this] { PrintStats(); });
    }
    
private:
    void OnConnection(const TcpConnectionPtr& conn) {
        if (conn->IsConnected()) {
            total_connections_++;
            current_connections_++;
            
            std::cout << "New connection from " 
                     << conn->GetPeerAddress().ToIpPortString() 
                     << std::endl;
            
            // 发送欢迎消息
            conn->Send("Welcome to Echo Server!\r\n");
        } else {
            current_connections_--;
        }
    }
    
    void OnMessage(const TcpConnectionPtr& conn, Buffer* buf) {
        total_messages_++;
        total_bytes_ += buf->ReadableBytes();
        
        // Echo回所有数据
        conn->Send(buf);
    }
    
    void PrintStats() {
        std::cout << "=== Server Statistics ===" << std::endl;
        std::cout << "Total connections: " << total_connections_ << std::endl;
        std::cout << "Current connections: " << current_connections_ << std::endl;
        std::cout << "Total messages: " << total_messages_ << std::endl;
        std::cout << "Total bytes: " << total_bytes_ << std::endl;
        
        // 计算吞吐量
        static int64_t last_messages = 0;
        static int64_t last_bytes = 0;
        
        int64_t msg_rate = (total_messages_ - last_messages) / 10;
        int64_t byte_rate = (total_bytes_ - last_bytes) / 10;
        
        std::cout << "Message rate: " << msg_rate << " msg/s" << std::endl;
        std::cout << "Byte rate: " << byte_rate / 1024 << " KB/s" << std::endl;
        
        last_messages = total_messages_.load();
        last_bytes = total_bytes_.load();
    }
};

int main(int argc, char* argv[]) {
    // 忽略SIGPIPE
    signal(SIGPIPE, SIG_IGN);
    
    EventLoop loop;
    InetAddress addr(7777);
    EchoServer server(&loop, addr);
    
    server.Start();
    loop.Loop();
    
    return 0;
}
```

## 📊 性能测试与优化

### 压力测试客户端
```cpp
class StressClient {
    EventLoop* loop_;
    std::vector<std::unique_ptr<TcpClient>> clients_;
    std::atomic<int64_t> total_sent_{0};
    std::atomic<int64_t> total_received_{0};
    
public:
    StressClient(EventLoop* loop, int num_clients, const InetAddress& server_addr)
        : loop_(loop) {
        
        for (int i = 0; i < num_clients; ++i) {
            auto client = std::make_unique<TcpClient>(loop, server_addr);
            
            client->SetConnectionCallback(
                [this, i](const TcpConnectionPtr& conn) {
                    if (conn->IsConnected()) {
                        // 开始发送数据
                        SendData(conn);
                    }
                });
            
            client->SetMessageCallback(
                [this](const TcpConnectionPtr& conn, Buffer* buf, Timestamp) {
                    total_received_ += buf->ReadableBytes();
                    buf->RetrieveAll();
                    
                    // 继续发送
                    SendData(conn);
                });
            
            clients_.push_back(std::move(client));
        }
    }
    
    void Start() {
        for (auto& client : clients_) {
            client->Connect();
        }
        
        // 定期打印统计
        loop_->RunEvery(1.0, [this] {
            std::cout << "Sent: " << total_sent_ 
                     << " Received: " << total_received_ << std::endl;
        });
    }
    
private:
    void SendData(const TcpConnectionPtr& conn) {
        std::string msg(1024, 'A');  // 1KB消息
        conn->Send(msg);
        total_sent_ += msg.size();
    }
};
```

## 🧪 练习与作业

### 练习1：实现定时器队列
使用最小堆或时间轮实现高性能定时器。

### 练习2：实现HTTP服务器
基于已有框架实现简单的HTTP/1.1服务器。

### 练习3：性能优化
- 实现零拷贝发送
- 优化内存分配
- 实现连接池

### 本周作业
1. 完成Echo服务器，支持10万并发连接
2. 实现聊天室服务器，支持房间和私聊
3. 编写性能测试报告，包括：
   - 不同并发数下的延迟分布
   - 吞吐量测试
   - CPU和内存使用情况
   - 性能瓶颈分析

## 📊 本周总结

### 掌握的技能
✅ 现代C++特性（RAII、移动语义、智能指针）
✅ Linux网络编程（epoll、非阻塞I/O）
✅ Reactor模式实现
✅ 高性能内存管理
✅ 多线程服务器架构

### 性能指标参考
- 单机10万并发连接
- 每秒100万消息处理
- 平均延迟 < 1ms
- CPU使用率 < 80%

## 🔮 下周预告

### 模块B预习内容
- ECS（Entity-Component-System）架构
- AOI（Area of Interest）算法
- 帧同步vs状态同步
- 游戏物理引擎集成

准备好进入游戏逻辑开发了吗？下周我们将构建真正的游戏服务器！