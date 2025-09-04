#pragma once

#include <string>
#include <memory>
#include <functional>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

namespace gameserver {

class InetAddress {
public:
    InetAddress() = default;
    explicit InetAddress(uint16_t port);
    InetAddress(const std::string& ip, uint16_t port);
    InetAddress(const struct sockaddr_in& addr) : addr_(addr) {}
    
    std::string ToIpString() const;
    std::string ToIpPortString() const;
    uint16_t Port() const;
    
    const struct sockaddr* GetSockAddr() const {
        return reinterpret_cast<const struct sockaddr*>(&addr_);
    }
    
    void SetSockAddr(const struct sockaddr_in& addr) { addr_ = addr; }
    
private:
    struct sockaddr_in addr_ = {0};
};

class Socket {
public:
    explicit Socket(int sockfd) : sockfd_(sockfd) {}
    ~Socket();
    
    // 禁止拷贝
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    
    // 支持移动
    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;
    
    int GetFd() const { return sockfd_; }
    
    // TCP服务器操作
    void Bind(const InetAddress& addr);
    void Listen(int backlog = SOMAXCONN);
    int Accept(InetAddress* peer_addr);
    
    // TCP客户端操作
    bool Connect(const InetAddress& addr);
    
    // 通用操作
    void ShutdownWrite();
    void SetReuseAddr(bool on);
    void SetReusePort(bool on);
    void SetKeepAlive(bool on);
    void SetTcpNoDelay(bool on);
    
    // 非阻塞设置
    void SetNonBlocking();
    void SetBlocking();
    
    // 读写操作
    ssize_t Read(void* buf, size_t len);
    ssize_t Write(const void* buf, size_t len);
    
    // 获取socket错误
    int GetSocketError();
    
    // 创建socket
    static int CreateNonblockingSocket();
    static int CreateBlockingSocket();
    
private:
    int sockfd_;
};

// Socket操作辅助函数
namespace sockets {
    
inline int GetSocketError(int sockfd) {
    int optval;
    socklen_t optlen = sizeof(optval);
    if (::getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &optval, &optlen) < 0) {
        return errno;
    }
    return optval;
}

inline void SetNonBlocking(int sockfd) {
    int flags = ::fcntl(sockfd, F_GETFL, 0);
    flags |= O_NONBLOCK;
    ::fcntl(sockfd, F_SETFL, flags);
}

inline void SetBlocking(int sockfd) {
    int flags = ::fcntl(sockfd, F_GETFL, 0);
    flags &= ~O_NONBLOCK;
    ::fcntl(sockfd, F_SETFL, flags);
}

inline struct sockaddr_in GetLocalAddr(int sockfd) {
    struct sockaddr_in local_addr;
    socklen_t addrlen = sizeof(local_addr);
    ::getsockname(sockfd, reinterpret_cast<struct sockaddr*>(&local_addr), &addrlen);
    return local_addr;
}

inline struct sockaddr_in GetPeerAddr(int sockfd) {
    struct sockaddr_in peer_addr;
    socklen_t addrlen = sizeof(peer_addr);
    ::getpeername(sockfd, reinterpret_cast<struct sockaddr*>(&peer_addr), &addrlen);
    return peer_addr;
}

inline bool IsSelfConnect(int sockfd) {
    struct sockaddr_in local_addr = GetLocalAddr(sockfd);
    struct sockaddr_in peer_addr = GetPeerAddr(sockfd);
    return local_addr.sin_port == peer_addr.sin_port &&
           local_addr.sin_addr.s_addr == peer_addr.sin_addr.s_addr;
}

} // namespace sockets

} // namespace gameserver