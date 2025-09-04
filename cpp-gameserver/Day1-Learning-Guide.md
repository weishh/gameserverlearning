# 📚 Day 1: 你的第一个C++游戏服务器

## ✅ 你已经完成了什么

恭喜！你已经成功：
1. ✅ 搭建了C++开发环境
2. ✅ 编译了Echo服务器
3. ✅ 运行并测试了服务器

## 🎯 今天的核心知识点

### 1. kqueue（macOS）/ epoll（Linux）事件模型

```cpp
// kqueue的工作原理（macOS）
int kq = kqueue();                    // 创建kqueue
struct kevent ev;
EV_SET(&ev, fd, EVFILT_READ, EV_ADD, 0, 0, NULL); // 注册事件
kevent(kq, &ev, 1, NULL, 0, NULL);   // 添加到kqueue

// 事件循环
struct kevent events[100];
int n = kevent(kq, NULL, 0, events, 100, NULL); // 等待事件
for(int i = 0; i < n; i++) {
    // 处理每个事件
}
```

**为什么使用kqueue/epoll？**
- select：O(n)复杂度，最多1024个fd
- poll：O(n)复杂度，无fd数量限制
- kqueue/epoll：O(1)复杂度，支持大量连接

### 2. 非阻塞I/O

```cpp
// 设置非阻塞
int flags = fcntl(fd, F_GETFL, 0);
fcntl(fd, F_SETFL, flags | O_NONBLOCK);

// 非阻塞读取
while(true) {
    ssize_t n = recv(fd, buffer, size, 0);
    if(n > 0) {
        // 处理数据
    } else if(n == 0) {
        // 连接关闭
    } else if(errno == EAGAIN) {
        // 没有数据可读，稍后再试
        break;
    }
}
```

### 3. 服务器架构模式

```
[客户端] ---> [Accept] ---> [Event Loop] ---> [Handler]
                 ↓              ↓                 ↓
            新建连接      事件分发          处理消息
```

## 🔨 动手练习（必做）

### 练习1：添加连接限制（15分钟）

修改 `simple_echo_server.cpp`，限制最大连接数：

```cpp
class SimpleEchoServer {
private:
    static constexpr int MAX_CONNECTIONS = 100;
    
    void AcceptConnection() {
        if (active_connections_ >= MAX_CONNECTIONS) {
            // 拒绝新连接
            int client_fd = accept(listen_fd_, nullptr, nullptr);
            if (client_fd >= 0) {
                const char* msg = "Server full, try later\r\n";
                send(client_fd, msg, strlen(msg), 0);
                close(client_fd);
            }
            return;
        }
        // ... 原来的代码
    }
};
```

### 练习2：实现Echo统计（20分钟）

为每个客户端统计echo的字节数：

```cpp
struct Client {
    int fd;
    std::string addr;
    std::vector<char> buffer;
    int64_t bytes_sent = 0;      // 添加
    int64_t bytes_received = 0;  // 添加
    int message_count = 0;        // 添加
};

void HandleRead(int fd) {
    // ... 读取数据
    if (n > 0) {
        it->second->bytes_received += n;
        it->second->message_count++;
        // ... echo回数据
        it->second->bytes_sent += n;
    }
}
```

### 练习3：添加简单命令（30分钟）

实现这些命令：
- `stats` - 显示当前连接的统计
- `who` - 列出所有连接
- `time` - 返回服务器时间

```cpp
void HandleCommand(int fd, const std::string& cmd) {
    if (cmd == "stats") {
        std::string stats = "Active: " + 
            std::to_string(active_connections_) + "\r\n";
        send(fd, stats.c_str(), stats.length(), 0);
    } else if (cmd == "who") {
        std::string list;
        for (const auto& [fd, client] : clients_) {
            list += client->addr + "\r\n";
        }
        send(fd, list.c_str(), list.length(), 0);
    }
    // ... 其他命令
}
```

## 📊 性能测试

### 测试1：延迟测试
```bash
# 测量单个请求的往返时间
time echo "test" | nc localhost 7777
```

### 测试2：并发测试
```bash
# 创建100个并发连接
for i in {1..100}; do
    (echo "Client $i"; sleep 10) | nc localhost 7777 &
done
```

### 测试3：吞吐量测试
```bash
./test_client bench  # 发送10000条消息
```

## 🐛 调试技巧

### 1. 使用日志
```cpp
#define DEBUG_LOG(msg) \
    std::cout << "[DEBUG] " << __FILE__ << ":" << __LINE__ \
              << " - " << msg << std::endl
```

### 2. 检查文件描述符泄漏
```bash
lsof -p $(pgrep echo_server) | wc -l
```

### 3. 内存泄漏检测
```bash
# macOS
leaks --atExit -- ./echo_server

# Linux
valgrind --leak-check=full ./echo_server
```

## 📖 深入理解

### 问题1：为什么要用非阻塞I/O？

**阻塞I/O的问题**：
```cpp
// 阻塞模式 - 一次只能处理一个客户端
while(true) {
    int client = accept(server_fd, ...);  // 阻塞
    char buf[1024];
    read(client, buf, 1024);  // 阻塞
    write(client, buf, 1024); // 阻塞
    close(client);
}
```

**非阻塞I/O的优势**：
```cpp
// 非阻塞 - 可以同时处理多个客户端
while(true) {
    for(auto event : wait_events()) {
        if(event.readable) {
            // 处理读事件，不会阻塞
        }
        if(event.writable) {
            // 处理写事件，不会阻塞
        }
    }
}
```

### 问题2：什么是惊群效应？

当多个进程/线程等待同一个事件时，事件发生后所有等待者都被唤醒，但只有一个能处理，其他的又回到等待状态。

解决方案：
- 使用SO_REUSEPORT（Linux 3.9+）
- 使用锁或其他同步机制
- 单线程accept，多线程处理

### 问题3：TCP_NODELAY的作用？

禁用Nagle算法，减少延迟：
```cpp
int flag = 1;
setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
```

适用场景：
- 游戏服务器（低延迟要求）
- 实时通信
- 小包传输

## 🎮 今晚作业

### 必做题
1. 完成上面的3个练习
2. 让服务器支持至少1000个并发连接
3. 实现一个简单的压力测试工具

### 选做题（挑战）
1. 实现消息广播功能
2. 添加心跳检测机制
3. 实现简单的聊天室（支持房间）

## 🔥 明天预告

明天我们将学习：
1. **内存池设计** - 减少动态分配
2. **线程池实现** - 多核并行处理
3. **协议设计** - 定义消息格式
4. **Buffer管理** - 高效的数据缓冲

## 💡 今日总结

你今天学到了：
- ✅ 事件驱动的服务器架构
- ✅ 非阻塞I/O的使用
- ✅ kqueue/epoll的基本原理
- ✅ 基本的网络编程技巧

**记住这句话**：
> "高性能服务器的核心是：永不阻塞，快速响应，充分利用每个CPU周期。"

明天见！继续加油！💪