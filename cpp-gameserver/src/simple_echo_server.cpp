// simple_echo_server.cpp - 你的第一个C++游戏服务器
#include <iostream>
#include <vector>
#include <unordered_map>
#include <memory>
#include <thread>
#include <atomic>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>

#ifdef __APPLE__
    #include <sys/event.h>
    #define USE_KQUEUE 1
#else
    #include <sys/epoll.h>
    #define USE_EPOLL 1
#endif

// 简单的日志宏
#define LOG(msg) std::cout << "[" << __TIME__ << "] " << msg << std::endl

class SimpleEchoServer {
private:
    int listen_fd_;
    int event_fd_;  // kqueue或epoll fd
    bool running_;
    
    // 客户端连接信息
    struct Client {
        int fd;
        std::string addr;
        std::vector<char> buffer;
    };
    std::unordered_map<int, std::unique_ptr<Client>> clients_;
    
    // 统计信息
    std::atomic<int> total_connections_{0};
    std::atomic<int> active_connections_{0};
    std::atomic<long> total_bytes_{0};
    
public:
    SimpleEchoServer(int port) : running_(false) {
        // 创建socket
        listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd_ < 0) {
            throw std::runtime_error("Failed to create socket");
        }
        
        // 设置socket选项
        int opt = 1;
        setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        
        // 绑定地址
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);
        
        if (bind(listen_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            close(listen_fd_);
            throw std::runtime_error("Failed to bind");
        }
        
        // 监听
        if (listen(listen_fd_, 128) < 0) {
            close(listen_fd_);
            throw std::runtime_error("Failed to listen");
        }
        
        // 设置非阻塞
        SetNonBlocking(listen_fd_);
        
#ifdef USE_KQUEUE
        // macOS: 创建kqueue
        event_fd_ = kqueue();
        if (event_fd_ < 0) {
            close(listen_fd_);
            throw std::runtime_error("Failed to create kqueue");
        }
        
        // 添加监听socket到kqueue
        struct kevent ev;
        EV_SET(&ev, listen_fd_, EVFILT_READ, EV_ADD, 0, 0, NULL);
        kevent(event_fd_, &ev, 1, NULL, 0, NULL);
#else
        // Linux: 创建epoll
        event_fd_ = epoll_create1(0);
        if (event_fd_ < 0) {
            close(listen_fd_);
            throw std::runtime_error("Failed to create epoll");
        }
        
        // 添加监听socket到epoll
        epoll_event ev{};
        ev.events = EPOLLIN;
        ev.data.fd = listen_fd_;
        epoll_ctl(event_fd_, EPOLL_CTL_ADD, listen_fd_, &ev);
#endif
        
        LOG("Server listening on port " << port);
    }
    
    ~SimpleEchoServer() {
        Stop();
        close(event_fd_);
        close(listen_fd_);
    }
    
    void Run() {
        running_ = true;
        
        // 启动统计线程
        std::thread stats_thread([this]() {
            while (running_) {
                std::this_thread::sleep_for(std::chrono::seconds(10));
                PrintStats();
            }
        });
        
#ifdef USE_KQUEUE
        RunKqueue();
#else
        RunEpoll();
#endif
        
        stats_thread.join();
    }
    
    void Stop() {
        running_ = false;
        clients_.clear();
    }
    
private:
    void SetNonBlocking(int fd) {
        int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
    
#ifdef USE_KQUEUE
    void RunKqueue() {
        struct kevent events[100];
        
        while (running_) {
            int nev = kevent(event_fd_, NULL, 0, events, 100, NULL);
            
            for (int i = 0; i < nev; i++) {
                if (events[i].ident == listen_fd_) {
                    // 新连接
                    AcceptConnection();
                } else if (events[i].filter == EVFILT_READ) {
                    // 可读事件
                    HandleRead(events[i].ident);
                }
            }
        }
    }
#else
    void RunEpoll() {
        epoll_event events[100];
        
        while (running_) {
            int nev = epoll_wait(event_fd_, events, 100, 1000);
            
            for (int i = 0; i < nev; i++) {
                if (events[i].data.fd == listen_fd_) {
                    // 新连接
                    AcceptConnection();
                } else if (events[i].events & EPOLLIN) {
                    // 可读事件
                    HandleRead(events[i].data.fd);
                }
            }
        }
    }
#endif
    
    void AcceptConnection() {
        while (true) {
            sockaddr_in client_addr{};
            socklen_t addr_len = sizeof(client_addr);
            
            int client_fd = accept(listen_fd_, (struct sockaddr*)&client_addr, &addr_len);
            if (client_fd < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;  // 没有更多连接
                }
                LOG("Accept error: " << strerror(errno));
                break;
            }
            
            SetNonBlocking(client_fd);
            
            // 创建客户端对象
            auto client = std::make_unique<Client>();
            client->fd = client_fd;
            client->addr = inet_ntoa(client_addr.sin_addr);
            client->addr += ":" + std::to_string(ntohs(client_addr.sin_port));
            client->buffer.reserve(4096);
            
            LOG("New connection from " << client->addr << " (fd=" << client_fd << ")");
            
            // 发送欢迎消息
            const char* welcome = "Welcome to Echo Server!\r\n";
            send(client_fd, welcome, strlen(welcome), 0);
            
#ifdef USE_KQUEUE
            // 添加到kqueue
            struct kevent ev;
            EV_SET(&ev, client_fd, EVFILT_READ, EV_ADD, 0, 0, NULL);
            kevent(event_fd_, &ev, 1, NULL, 0, NULL);
#else
            // 添加到epoll
            epoll_event ev{};
            ev.events = EPOLLIN;
            ev.data.fd = client_fd;
            epoll_ctl(event_fd_, EPOLL_CTL_ADD, client_fd, &ev);
#endif
            
            clients_[client_fd] = std::move(client);
            total_connections_++;
            active_connections_++;
        }
    }
    
    void HandleRead(int fd) {
        auto it = clients_.find(fd);
        if (it == clients_.end()) {
            return;
        }
        
        char buffer[1024];
        while (true) {
            ssize_t n = recv(fd, buffer, sizeof(buffer), 0);
            
            if (n > 0) {
                // Echo回数据
                send(fd, buffer, n, 0);
                total_bytes_ += n;
                
                // 检查是否是退出命令
                if (n >= 4 && strncmp(buffer, "quit", 4) == 0) {
                    const char* bye = "Goodbye!\r\n";
                    send(fd, bye, strlen(bye), 0);
                    CloseClient(fd);
                    return;
                }
            } else if (n == 0) {
                // 连接关闭
                LOG("Connection closed by " << it->second->addr);
                CloseClient(fd);
                return;
            } else {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;  // 没有更多数据
                }
                LOG("Read error from " << it->second->addr << ": " << strerror(errno));
                CloseClient(fd);
                return;
            }
        }
    }
    
    void CloseClient(int fd) {
        auto it = clients_.find(fd);
        if (it != clients_.end()) {
            LOG("Closing connection to " << it->second->addr);
            
#ifdef USE_KQUEUE
            struct kevent ev;
            EV_SET(&ev, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
            kevent(event_fd_, &ev, 1, NULL, 0, NULL);
#else
            epoll_ctl(event_fd_, EPOLL_CTL_DEL, fd, NULL);
#endif
            
            close(fd);
            clients_.erase(it);
            active_connections_--;
        }
    }
    
    void PrintStats() {
        std::cout << "\n========== Server Statistics ==========\n";
        std::cout << "Total connections: " << total_connections_.load() << "\n";
        std::cout << "Active connections: " << active_connections_.load() << "\n";
        std::cout << "Total bytes echoed: " << total_bytes_.load() << "\n";
        std::cout << "=======================================\n" << std::endl;
    }
};

int main(int argc, char* argv[]) {
    // 忽略SIGPIPE信号（当写入已关闭的socket时产生）
    signal(SIGPIPE, SIG_IGN);
    
    int port = 7777;
    if (argc > 1) {
        port = std::atoi(argv[1]);
    }
    
    try {
        std::cout << "Starting Echo Server on port " << port << "...\n";
        std::cout << "Commands:\n";
        std::cout << "  - Type any text to echo it back\n";
        std::cout << "  - Type 'quit' to disconnect\n";
        std::cout << "  - Press Ctrl+C to stop server\n\n";
        
        SimpleEchoServer server(port);
        server.Run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}