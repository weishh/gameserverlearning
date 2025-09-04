# 🚀 C++游戏服务器开发 - 立即开始

## ✅ 第1步：编译服务器
```bash
make
```

## ✅ 第2步：启动服务器
打开第一个终端：
```bash
./echo_server
```

## ✅ 第3步：测试连接
打开第二个终端：
```bash
# 方法1：使用telnet
telnet localhost 7777

# 方法2：使用nc
nc localhost 7777

# 方法3：编译并运行测试客户端
clang++ -std=c++17 src/test_client.cpp -o test_client
./test_client
```

## 📝 今天的学习任务（Day 1）

### 上午（2小时）：理解代码
1. **阅读 `simple_echo_server.cpp`**
   - 理解kqueue（macOS）的事件模型
   - 理解socket的创建、绑定、监听流程
   - 理解非阻塞I/O的设置

2. **关键概念理解**：
   ```cpp
   // kqueue的事件注册
   struct kevent ev;
   EV_SET(&ev, fd, EVFILT_READ, EV_ADD, 0, 0, NULL);
   
   // 非阻塞socket
   fcntl(fd, F_SETFL, O_NONBLOCK);
   
   // 事件循环
   while(running) {
       int n = kevent(kq, NULL, 0, events, 100, NULL);
       // 处理n个事件
   }
   ```

### 下午（3小时）：动手实践

#### 练习1：添加消息计数
修改服务器，统计每个客户端发送的消息数：
```cpp
struct Client {
    int fd;
    std::string addr;
    int message_count;  // 添加这个
};
```

#### 练习2：实现广播功能
当收到消息时，将其广播给所有连接的客户端：
```cpp
void Broadcast(const std::string& msg, int except_fd) {
    for (auto& [fd, client] : clients_) {
        if (fd != except_fd) {
            send(fd, msg.c_str(), msg.length(), 0);
        }
    }
}
```

#### 练习3：添加命令处理
实现以下命令：
- `/list` - 列出所有连接的客户端
- `/stats` - 显示服务器统计
- `/kick <fd>` - 踢出指定客户端

### 晚上（1小时）：性能测试

1. **单连接性能测试**
```bash
./test_client bench
```

2. **多连接压力测试**
```bash
./test_client stress 100 1000  # 100个客户端，每个发1000条消息
```

3. **监控系统资源**
```bash
# 另开终端监控
top -pid $(pgrep echo_server)
```

## 🎯 第一周目标

### Day 1-2: 基础网络编程
- [x] 编译运行Echo服务器
- [ ] 理解事件驱动模型
- [ ] 完成3个练习

### Day 3-4: 优化与扩展
- [ ] 实现内存池
- [ ] 添加线程池
- [ ] 支持1万并发连接

### Day 5-6: 协议设计
- [ ] 设计二进制协议
- [ ] 实现消息序列化
- [ ] 添加心跳机制

### Day 7: 项目实战
- [ ] 完成聊天室服务器
- [ ] 支持房间管理
- [ ] 性能测试报告

## 📚 今晚阅读材料

1. **理解I/O多路复用**
   - kqueue vs epoll vs select
   - 为什么游戏服务器需要非阻塞I/O

2. **C++内存管理**
   - RAII原则
   - 智能指针的使用
   - 内存池的设计

3. **网络编程最佳实践**
   - TCP_NODELAY的作用
   - SO_REUSEADDR的意义
   - Nagle算法

## ⚠️ 常见问题

### Q: Address already in use
```bash
# 查找占用端口的进程
lsof -i :7777
# 杀死进程
kill -9 <PID>
```

### Q: Too many open files
```bash
# 查看当前限制
ulimit -n
# 增加限制
ulimit -n 10000
```

### Q: 如何调试？
```bash
# 使用lldb调试
lldb ./echo_server
(lldb) run
(lldb) bt  # 查看调用栈
```

## 💪 挑战任务

完成基础任务后，尝试这些挑战：

1. **性能优化**：让服务器支持10万并发连接
2. **协议升级**：实现WebSocket协议
3. **分布式**：实现多个服务器之间的消息转发

---

## 🎉 恭喜你迈出了第一步！

现在你已经有了一个可以运行的游戏服务器。虽然它很简单，但包含了游戏服务器的核心要素：
- 事件驱动架构
- 非阻塞I/O
- 连接管理
- 消息处理

明天我们将深入学习如何优化性能，添加更多功能。

**记住**：优秀的游戏服务器开发者是通过不断实践成长的。每天写代码，每天进步一点点！

有问题随时问我！加油！💪