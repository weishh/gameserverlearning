#include <iostream>
#include <memory>
#include <string>
#include <functional>
#include <signal.h>
#include "network/tcp_server.h"
#include "network/event_loop.h"
#include "network/buffer.h"
#include "utils/logger.h"

using namespace gameserver;

class EchoServer {
public:
    EchoServer(EventLoop* loop, const InetAddress& listen_addr)
        : server_(loop, listen_addr, "EchoServer"),
          connections_(0),
          messages_received_(0),
          bytes_received_(0) {
        
        // 设置连接回调
        server_.SetConnectionCallback(
            std::bind(&EchoServer::OnConnection, this, std::placeholders::_1));
        
        // 设置消息回调
        server_.SetMessageCallback(
            std::bind(&EchoServer::OnMessage, this, 
                     std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
        
        // 设置线程数
        server_.SetThreadNum(4);
    }
    
    void Start() {
        LOG_INFO << "EchoServer starting on " << server_.GetListenAddress().ToIpPortString();
        server_.Start();
    }
    
    void PrintStatistics() const {
        LOG_INFO << "=== Server Statistics ===";
        LOG_INFO << "Active connections: " << connections_;
        LOG_INFO << "Messages received: " << messages_received_;
        LOG_INFO << "Bytes received: " << bytes_received_;
        LOG_INFO << "========================";
    }
    
private:
    void OnConnection(const TcpConnectionPtr& conn) {
        LOG_INFO << "Connection " << conn->GetName() << " from " 
                << conn->GetPeerAddress().ToIpPortString()
                << " is " << (conn->IsConnected() ? "UP" : "DOWN");
        
        if (conn->IsConnected()) {
            connections_++;
            
            // 发送欢迎消息
            std::string welcome = "Welcome to Echo Server!\r\n";
            conn->Send(welcome);
            
            // 启用TCP_NODELAY
            conn->SetTcpNoDelay(true);
        } else {
            connections_--;
        }
    }
    
    void OnMessage(const TcpConnectionPtr& conn, Buffer* buf, Timestamp receive_time) {
        messages_received_++;
        bytes_received_ += buf->ReadableBytes();
        
        // Echo所有接收到的数据
        std::string msg(buf->RetrieveAllAsString());
        LOG_DEBUG << conn->GetName() << " echo " << msg.size() << " bytes, "
                 << "data received at " << receive_time.ToString();
        
        // 发送回客户端
        conn->Send(msg);
        
        // 处理特殊命令
        if (msg == "quit\r\n" || msg == "quit\n") {
            conn->Send("Bye!\r\n");
            conn->Shutdown();
        } else if (msg == "stats\r\n" || msg == "stats\n") {
            std::string stats = "Active connections: " + std::to_string(connections_) + "\r\n" +
                              "Messages received: " + std::to_string(messages_received_) + "\r\n" +
                              "Bytes received: " + std::to_string(bytes_received_) + "\r\n";
            conn->Send(stats);
        }
    }
    
private:
    TcpServer server_;
    std::atomic<int32_t> connections_;
    std::atomic<int64_t> messages_received_;
    std::atomic<int64_t> bytes_received_;
};

int main(int argc, char* argv[]) {
    // 初始化日志
    Logger::SetLogLevel(Logger::DEBUG);
    
    // 忽略SIGPIPE信号
    signal(SIGPIPE, SIG_IGN);
    
    // 解析端口
    int port = 7777;
    if (argc > 1) {
        port = std::atoi(argv[1]);
    }
    
    LOG_INFO << "Starting Echo Server on port " << port;
    
    EventLoop loop;
    InetAddress listen_addr(port);
    EchoServer server(&loop, listen_addr);
    
    // 启动服务器
    server.Start();
    
    // 定期打印统计信息
    loop.RunEvery(30.0, [&server]() {
        server.PrintStatistics();
    });
    
    // 运行事件循环
    loop.Loop();
    
    return 0;
}