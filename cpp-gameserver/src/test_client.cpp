// test_client.cpp - 测试客户端
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

class TestClient {
private:
    int sock_fd_;
    std::string server_addr_;
    int server_port_;
    bool connected_;
    
public:
    TestClient(const std::string& addr, int port) 
        : server_addr_(addr), server_port_(port), connected_(false) {
        sock_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (sock_fd_ < 0) {
            throw std::runtime_error("Failed to create socket");
        }
    }
    
    ~TestClient() {
        if (connected_) {
            close(sock_fd_);
        }
    }
    
    bool Connect() {
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(server_port_);
        inet_pton(AF_INET, server_addr_.c_str(), &addr.sin_addr);
        
        if (connect(sock_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            std::cerr << "Failed to connect: " << strerror(errno) << std::endl;
            return false;
        }
        
        connected_ = true;
        std::cout << "Connected to " << server_addr_ << ":" << server_port_ << std::endl;
        
        // 接收欢迎消息
        char buffer[256];
        int n = recv(sock_fd_, buffer, sizeof(buffer)-1, 0);
        if (n > 0) {
            buffer[n] = '\0';
            std::cout << "Server: " << buffer;
        }
        
        return true;
    }
    
    void RunInteractive() {
        if (!connected_) return;
        
        // 启动接收线程
        std::thread recv_thread([this]() {
            char buffer[1024];
            while (connected_) {
                int n = recv(sock_fd_, buffer, sizeof(buffer)-1, 0);
                if (n > 0) {
                    buffer[n] = '\0';
                    std::cout << "Echo: " << buffer;
                } else if (n == 0) {
                    std::cout << "\nServer closed connection" << std::endl;
                    connected_ = false;
                    break;
                }
            }
        });
        
        // 主线程发送数据
        std::cout << "Type messages (or 'quit' to exit):" << std::endl;
        std::string line;
        while (connected_ && std::getline(std::cin, line)) {
            if (line == "quit") {
                send(sock_fd_, "quit", 4, 0);
                break;
            }
            
            line += "\n";
            send(sock_fd_, line.c_str(), line.length(), 0);
        }
        
        connected_ = false;
        recv_thread.join();
    }
    
    void RunStressTest(int num_messages) {
        if (!connected_) return;
        
        std::cout << "Sending " << num_messages << " messages..." << std::endl;
        auto start = std::chrono::high_resolution_clock::now();
        
        for (int i = 0; i < num_messages; i++) {
            std::string msg = "Message " + std::to_string(i) + "\n";
            send(sock_fd_, msg.c_str(), msg.length(), 0);
            
            char buffer[1024];
            recv(sock_fd_, buffer, sizeof(buffer), 0);
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        
        std::cout << "Completed " << num_messages << " echo requests in " 
                  << duration.count() << " ms" << std::endl;
        std::cout << "Rate: " << (num_messages * 1000.0 / duration.count()) 
                  << " msg/sec" << std::endl;
        
        send(sock_fd_, "quit", 4, 0);
    }
};

void RunMultiClientStress(int num_clients, int messages_per_client) {
    std::vector<std::thread> threads;
    
    std::cout << "Starting " << num_clients << " clients..." << std::endl;
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < num_clients; i++) {
        threads.emplace_back([i, messages_per_client]() {
            try {
                TestClient client("127.0.0.1", 7777);
                if (client.Connect()) {
                    for (int j = 0; j < messages_per_client; j++) {
                        std::string msg = "Client" + std::to_string(i) + 
                                        "_Msg" + std::to_string(j) + "\n";
                        // 这里简化处理，实际应该完整实现
                    }
                }
            } catch (const std::exception& e) {
                std::cerr << "Client " << i << " error: " << e.what() << std::endl;
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << "\n=== Stress Test Results ===" << std::endl;
    std::cout << "Clients: " << num_clients << std::endl;
    std::cout << "Total messages: " << (num_clients * messages_per_client) << std::endl;
    std::cout << "Total time: " << duration.count() << " ms" << std::endl;
    std::cout << "Throughput: " 
              << (num_clients * messages_per_client * 1000.0 / duration.count()) 
              << " msg/sec" << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc > 1 && std::string(argv[1]) == "stress") {
        // 压力测试模式
        int clients = (argc > 2) ? std::atoi(argv[2]) : 10;
        int messages = (argc > 3) ? std::atoi(argv[3]) : 100;
        RunMultiClientStress(clients, messages);
    } else if (argc > 1 && std::string(argv[1]) == "bench") {
        // 性能测试模式
        try {
            TestClient client("127.0.0.1", 7777);
            if (client.Connect()) {
                client.RunStressTest(10000);
            }
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
        }
    } else {
        // 交互模式
        try {
            TestClient client("127.0.0.1", 7777);
            if (client.Connect()) {
                client.RunInteractive();
            }
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
        }
    }
    
    return 0;
}