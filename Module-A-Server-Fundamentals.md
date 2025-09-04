# 模块 A: 游戏服务端基础（Week 1-2）

## 📚 第一周：核心概念与基础网络

### 1. 游戏服务器的职责

#### 概念讲解（通俗版）
想象游戏服务器就像一个"裁判"+"记录员"的组合：
- **裁判职责**：验证玩家的动作是否合法（反作弊）
- **记录员职责**：保存游戏状态，确保所有玩家看到一致的世界
- **协调者职责**：同步不同玩家之间的交互

#### 深入原理
游戏服务器的核心职责包括：

1. **状态管理（State Management）**
   - 权威性状态（Authoritative State）：服务器是游戏世界的唯一真相源
   - 状态快照（Snapshots）：定期保存完整状态用于恢复
   - 增量更新（Delta Updates）：只发送变化的部分以节省带宽

2. **客户端验证（Client Validation）**
   - 输入验证：检查移动速度、攻击频率是否合理
   - 业务逻辑验证：验证游戏规则（如：是否有足够金币购买物品）
   - 时序验证：检查操作顺序是否合理

3. **网络通信管理**
   - 连接管理：处理玩家的加入/离开
   - 消息路由：将消息发送给正确的玩家
   - 流量控制：防止网络拥塞

### 2. TCP vs UDP 选择策略

#### 概念讲解（通俗版）
- **TCP**：像打电话，保证对方一定能听到，且顺序正确，但可能有延迟
- **UDP**：像广播，速度快但可能丢失，需要自己处理可靠性

#### 深入原理与选择策略

```plaintext
TCP适用场景：
├── 回合制游戏（如卡牌游戏）
├── MMORPG的非战斗系统（交易、聊天）
└── 需要可靠传输的关键数据

UDP适用场景：
├── FPS游戏（低延迟至关重要）
├── MOBA游戏的实时战斗
└── 位置同步等高频更新数据

混合方案：
└── TCP处理可靠数据 + UDP处理实时数据
```

### 3. 网络延迟处理基础

#### RTT（Round-Trip Time）测量
```csharp
public class NetworkLatency
{
    private readonly Dictionary<int, DateTime> _pendingPings = new();
    private double _averageRtt = 0;
    private readonly Queue<double> _rttHistory = new(capacity: 10);
    
    public void SendPing(int sequenceNumber)
    {
        _pendingPings[sequenceNumber] = DateTime.UtcNow;
        // 发送ping包到客户端
    }
    
    public void ReceivePong(int sequenceNumber)
    {
        if (_pendingPings.TryGetValue(sequenceNumber, out DateTime sentTime))
        {
            double rtt = (DateTime.UtcNow - sentTime).TotalMilliseconds;
            _pendingPings.Remove(sequenceNumber);
            
            // 维护滑动窗口平均值
            _rttHistory.Enqueue(rtt);
            if (_rttHistory.Count > 10)
                _rttHistory.Dequeue();
            
            _averageRtt = _rttHistory.Average();
        }
    }
    
    public double GetAverageRtt() => _averageRtt;
    public double GetCurrentLatency() => _averageRtt / 2; // 单程延迟估算
}
```

#### 时间同步
```csharp
public class TimeSync
{
    private double _serverTimeOffset = 0;
    
    public void SyncWithServer(long serverTimestamp, double rtt)
    {
        // 考虑网络延迟的时间同步
        long localTime = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
        long estimatedServerTime = serverTimestamp + (long)(rtt / 2);
        _serverTimeOffset = estimatedServerTime - localTime;
    }
    
    public long GetSynchronizedTime()
    {
        return DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() + (long)_serverTimeOffset;
    }
}
```

## 💻 第一周实战：Echo服务器

### C# 实现（使用.NET Core）

```csharp
// EchoServer.cs
using System;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Collections.Concurrent;

public class EchoServer
{
    private TcpListener _listener;
    private readonly ConcurrentDictionary<Guid, ClientHandler> _clients = new();
    private readonly int _port;
    private CancellationTokenSource _cancellationTokenSource;
    
    public EchoServer(int port = 7777)
    {
        _port = port;
    }
    
    public async Task StartAsync()
    {
        _listener = new TcpListener(IPAddress.Any, _port);
        _listener.Start();
        _cancellationTokenSource = new CancellationTokenSource();
        
        Console.WriteLine($"Echo Server started on port {_port}");
        
        while (!_cancellationTokenSource.Token.IsCancellationRequested)
        {
            try
            {
                var tcpClient = await _listener.AcceptTcpClientAsync();
                var clientId = Guid.NewGuid();
                var handler = new ClientHandler(clientId, tcpClient, this);
                
                _clients[clientId] = handler;
                _ = Task.Run(() => handler.HandleAsync(_cancellationTokenSource.Token));
                
                Console.WriteLine($"Client {clientId} connected from {tcpClient.Client.RemoteEndPoint}");
            }
            catch (ObjectDisposedException)
            {
                break;
            }
        }
    }
    
    public void RemoveClient(Guid clientId)
    {
        if (_clients.TryRemove(clientId, out var client))
        {
            Console.WriteLine($"Client {clientId} disconnected");
            client.Dispose();
        }
    }
    
    public void BroadcastMessage(string message, Guid senderId)
    {
        var timestamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
        var formattedMessage = $"[{timestamp}] Client {senderId}: {message}";
        
        foreach (var client in _clients.Values)
        {
            if (client.ClientId != senderId)
            {
                client.SendMessage(formattedMessage);
            }
        }
    }
    
    public void Stop()
    {
        _cancellationTokenSource?.Cancel();
        _listener?.Stop();
        
        foreach (var client in _clients.Values)
        {
            client.Dispose();
        }
        _clients.Clear();
    }
}

public class ClientHandler : IDisposable
{
    public Guid ClientId { get; }
    private readonly TcpClient _tcpClient;
    private readonly NetworkStream _stream;
    private readonly EchoServer _server;
    
    public ClientHandler(Guid clientId, TcpClient tcpClient, EchoServer server)
    {
        ClientId = clientId;
        _tcpClient = tcpClient;
        _stream = tcpClient.GetStream();
        _server = server;
    }
    
    public async Task HandleAsync(CancellationToken cancellationToken)
    {
        var buffer = new byte[1024];
        
        try
        {
            while (!cancellationToken.IsCancellationRequested && _tcpClient.Connected)
            {
                int bytesRead = await _stream.ReadAsync(buffer, 0, buffer.Length, cancellationToken);
                
                if (bytesRead == 0)
                    break;
                
                string message = Encoding.UTF8.GetString(buffer, 0, bytesRead);
                Console.WriteLine($"Received from {ClientId}: {message}");
                
                // Echo back to sender
                await _stream.WriteAsync(buffer, 0, bytesRead, cancellationToken);
                
                // Broadcast to other clients
                _server.BroadcastMessage(message, ClientId);
            }
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Error handling client {ClientId}: {ex.Message}");
        }
        finally
        {
            _server.RemoveClient(ClientId);
        }
    }
    
    public void SendMessage(string message)
    {
        try
        {
            var data = Encoding.UTF8.GetBytes(message);
            _stream.Write(data, 0, data.Length);
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Error sending message to client {ClientId}: {ex.Message}");
        }
    }
    
    public void Dispose()
    {
        _stream?.Dispose();
        _tcpClient?.Close();
    }
}

// Program.cs - 启动服务器
class Program
{
    static async Task Main(string[] args)
    {
        var server = new EchoServer(7777);
        
        var serverTask = server.StartAsync();
        
        Console.WriteLine("Press 'Q' to quit");
        while (Console.ReadKey().Key != ConsoleKey.Q) { }
        
        server.Stop();
        Console.WriteLine("Server stopped");
    }
}
```

### Node.js (TypeScript) 实现

```typescript
// echoServer.ts
import net from 'net';
import { EventEmitter } from 'events';

interface Client {
    id: string;
    socket: net.Socket;
    address: string;
    lastActivity: number;
}

class EchoServer extends EventEmitter {
    private server: net.Server;
    private clients: Map<string, Client> = new Map();
    private port: number;
    
    constructor(port: number = 7777) {
        super();
        this.port = port;
        this.server = net.createServer();
        this.setupServer();
    }
    
    private setupServer(): void {
        this.server.on('connection', this.handleConnection.bind(this));
        this.server.on('error', this.handleServerError.bind(this));
        this.server.on('listening', () => {
            console.log(`Echo Server listening on port ${this.port}`);
        });
    }
    
    private handleConnection(socket: net.Socket): void {
        const clientId = this.generateClientId();
        const client: Client = {
            id: clientId,
            socket: socket,
            address: `${socket.remoteAddress}:${socket.remotePort}`,
            lastActivity: Date.now()
        };
        
        this.clients.set(clientId, client);
        console.log(`Client ${clientId} connected from ${client.address}`);
        
        // 发送欢迎消息
        socket.write(`Welcome! Your ID is ${clientId}\n`);
        
        // 设置socket事件处理
        socket.on('data', (data) => this.handleClientData(clientId, data));
        socket.on('error', (err) => this.handleClientError(clientId, err));
        socket.on('close', () => this.handleClientDisconnect(clientId));
        
        // 设置keep-alive
        socket.setKeepAlive(true, 10000);
    }
    
    private handleClientData(clientId: string, data: Buffer): void {
        const client = this.clients.get(clientId);
        if (!client) return;
        
        const message = data.toString().trim();
        const timestamp = Date.now();
        
        client.lastActivity = timestamp;
        
        console.log(`[${timestamp}] Received from ${clientId}: ${message}`);
        
        // Echo back to sender
        client.socket.write(`Echo: ${message}\n`);
        
        // Broadcast to other clients
        this.broadcast(`[${clientId}]: ${message}\n`, clientId);
    }
    
    private handleClientError(clientId: string, error: Error): void {
        console.error(`Client ${clientId} error:`, error.message);
    }
    
    private handleClientDisconnect(clientId: string): void {
        const client = this.clients.get(clientId);
        if (client) {
            console.log(`Client ${clientId} disconnected`);
            this.clients.delete(clientId);
            this.broadcast(`Client ${clientId} has left\n`, clientId);
        }
    }
    
    private handleServerError(error: Error): void {
        console.error('Server error:', error);
    }
    
    private broadcast(message: string, excludeClientId?: string): void {
        this.clients.forEach((client, id) => {
            if (id !== excludeClientId) {
                client.socket.write(message);
            }
        });
    }
    
    private generateClientId(): string {
        return `client_${Date.now()}_${Math.random().toString(36).substr(2, 9)}`;
    }
    
    public start(): void {
        this.server.listen(this.port);
    }
    
    public stop(): void {
        // 断开所有客户端
        this.clients.forEach(client => {
            client.socket.end('Server shutting down\n');
        });
        
        this.server.close();
        console.log('Server stopped');
    }
    
    // 获取服务器统计信息
    public getStats(): object {
        return {
            clientCount: this.clients.size,
            clients: Array.from(this.clients.values()).map(c => ({
                id: c.id,
                address: c.address,
                lastActivity: c.lastActivity
            }))
        };
    }
}

// 使用示例
const server = new EchoServer(7777);
server.start();

// 定期打印统计信息
setInterval(() => {
    console.log('Server Stats:', server.getStats());
}, 30000);

// 优雅关闭
process.on('SIGINT', () => {
    console.log('\nShutting down server...');
    server.stop();
    process.exit(0);
});
```

## 📚 第二周：高级网络与序列化

### 1. 消息序列化方案对比

#### Protocol Buffers 实现
```protobuf
// messages.proto
syntax = "proto3";

package gameserver;

message PlayerPosition {
    float x = 1;
    float y = 2;
    float z = 3;
    float rotation = 4;
    int64 timestamp = 5;
}

message GameMessage {
    enum MessageType {
        UNKNOWN = 0;
        PLAYER_MOVE = 1;
        PLAYER_ATTACK = 2;
        CHAT = 3;
        PING = 4;
        PONG = 5;
    }
    
    MessageType type = 1;
    int32 player_id = 2;
    
    oneof payload {
        PlayerPosition position = 3;
        string chat_message = 4;
        int64 ping_timestamp = 5;
    }
}
```

#### C# 使用 Protobuf
```csharp
using Google.Protobuf;
using System.IO;

public class MessageSerializer
{
    public byte[] Serialize(GameMessage message)
    {
        using var stream = new MemoryStream();
        message.WriteTo(stream);
        return stream.ToArray();
    }
    
    public GameMessage Deserialize(byte[] data)
    {
        return GameMessage.Parser.ParseFrom(data);
    }
    
    // 性能优化：对象池
    private readonly Stack<GameMessage> _messagePool = new();
    
    public GameMessage GetPooledMessage()
    {
        if (_messagePool.Count > 0)
            return _messagePool.Pop();
        return new GameMessage();
    }
    
    public void ReturnToPool(GameMessage message)
    {
        message.Clear();
        _messagePool.Push(message);
    }
}
```

### 2. UDP可靠传输实现

```csharp
// ReliableUDP.cs
public class ReliableUdpConnection
{
    private readonly UdpClient _udpClient;
    private readonly IPEndPoint _remoteEndPoint;
    private ushort _sequenceNumber = 0;
    private ushort _ackNumber = 0;
    
    // 发送窗口
    private readonly Dictionary<ushort, PendingPacket> _pendingPackets = new();
    // 接收窗口
    private readonly SortedDictionary<ushort, byte[]> _receiveBuffer = new();
    private ushort _expectedSequence = 0;
    
    private class PendingPacket
    {
        public byte[] Data { get; set; }
        public DateTime SentTime { get; set; }
        public int RetryCount { get; set; }
    }
    
    public async Task SendReliableAsync(byte[] data)
    {
        var packet = new UdpPacket
        {
            SequenceNumber = _sequenceNumber++,
            AckNumber = _ackNumber,
            IsAck = false,
            Data = data
        };
        
        var serialized = SerializePacket(packet);
        
        // 记录待确认包
        _pendingPackets[packet.SequenceNumber] = new PendingPacket
        {
            Data = serialized,
            SentTime = DateTime.UtcNow,
            RetryCount = 0
        };
        
        await _udpClient.SendAsync(serialized, serialized.Length, _remoteEndPoint);
        
        // 启动重传定时器
        _ = Task.Run(() => RetransmitIfNeeded(packet.SequenceNumber));
    }
    
    private async Task RetransmitIfNeeded(ushort sequenceNumber)
    {
        await Task.Delay(100); // RTT估算值
        
        if (_pendingPackets.TryGetValue(sequenceNumber, out var pending))
        {
            if (pending.RetryCount < 3)
            {
                pending.RetryCount++;
                pending.SentTime = DateTime.UtcNow;
                await _udpClient.SendAsync(pending.Data, pending.Data.Length, _remoteEndPoint);
                
                // 指数退避
                await Task.Delay(100 * (int)Math.Pow(2, pending.RetryCount));
                _ = Task.Run(() => RetransmitIfNeeded(sequenceNumber));
            }
            else
            {
                // 连接可能已断开
                OnConnectionLost?.Invoke();
            }
        }
    }
    
    public event Action OnConnectionLost;
    
    private struct UdpPacket
    {
        public ushort SequenceNumber;
        public ushort AckNumber;
        public bool IsAck;
        public byte[] Data;
    }
    
    private byte[] SerializePacket(UdpPacket packet)
    {
        using var ms = new MemoryStream();
        using var writer = new BinaryWriter(ms);
        
        writer.Write(packet.SequenceNumber);
        writer.Write(packet.AckNumber);
        writer.Write(packet.IsAck);
        writer.Write(packet.Data.Length);
        writer.Write(packet.Data);
        
        return ms.ToArray();
    }
}
```

## 🎮 第二周实战：聊天室服务器升级

将Echo服务器升级为功能完整的聊天室，包含：
- 用户认证
- 房间管理
- 消息历史
- 在线状态

```csharp
// ChatRoomServer.cs
public class ChatRoomServer
{
    private readonly Dictionary<string, ChatRoom> _rooms = new();
    private readonly Dictionary<string, User> _users = new();
    
    public class User
    {
        public string Id { get; set; }
        public string Name { get; set; }
        public TcpClient Client { get; set; }
        public string CurrentRoom { get; set; }
        public DateTime LastActivity { get; set; }
    }
    
    public class ChatRoom
    {
        public string Id { get; set; }
        public string Name { get; set; }
        public HashSet<string> Users { get; } = new();
        public Queue<ChatMessage> MessageHistory { get; } = new();
        private const int MaxHistorySize = 100;
        
        public void AddMessage(ChatMessage message)
        {
            MessageHistory.Enqueue(message);
            if (MessageHistory.Count > MaxHistorySize)
                MessageHistory.Dequeue();
        }
    }
    
    public class ChatMessage
    {
        public string UserId { get; set; }
        public string Content { get; set; }
        public DateTime Timestamp { get; set; }
    }
    
    public void HandleUserJoinRoom(string userId, string roomId)
    {
        if (!_users.TryGetValue(userId, out var user))
            return;
        
        // 离开当前房间
        if (!string.IsNullOrEmpty(user.CurrentRoom))
        {
            if (_rooms.TryGetValue(user.CurrentRoom, out var oldRoom))
            {
                oldRoom.Users.Remove(userId);
                BroadcastToRoom(oldRoom.Id, $"{user.Name} has left the room");
            }
        }
        
        // 加入新房间
        if (!_rooms.ContainsKey(roomId))
        {
            _rooms[roomId] = new ChatRoom { Id = roomId, Name = $"Room {roomId}" };
        }
        
        var room = _rooms[roomId];
        room.Users.Add(userId);
        user.CurrentRoom = roomId;
        
        // 发送历史消息
        SendMessageHistory(user, room);
        
        BroadcastToRoom(roomId, $"{user.Name} has joined the room");
    }
    
    private void SendMessageHistory(User user, ChatRoom room)
    {
        foreach (var message in room.MessageHistory)
        {
            var historyData = $"[History] {message.Timestamp:HH:mm:ss} {message.UserId}: {message.Content}\n";
            SendToUser(user, historyData);
        }
    }
    
    private void BroadcastToRoom(string roomId, string message)
    {
        if (!_rooms.TryGetValue(roomId, out var room))
            return;
        
        foreach (var userId in room.Users)
        {
            if (_users.TryGetValue(userId, out var user))
            {
                SendToUser(user, message);
            }
        }
    }
    
    private void SendToUser(User user, string message)
    {
        try
        {
            var data = Encoding.UTF8.GetBytes(message);
            user.Client.GetStream().Write(data, 0, data.Length);
        }
        catch
        {
            // Handle disconnection
        }
    }
}
```

## 🧪 练习与测验

### 练习1：实现心跳机制
为Echo服务器添加心跳机制，要求：
1. 客户端每30秒发送一次心跳
2. 服务器超过60秒未收到心跳则断开连接
3. 实现自动重连机制

### 练习2：消息队列实现
实现一个高性能的消息队列系统：
1. 支持优先级队列
2. 实现背压（Backpressure）机制
3. 添加消息持久化

### 练习3：性能测试
编写压力测试脚本：
1. 模拟1000个并发客户端
2. 测量消息延迟分布
3. 找出性能瓶颈并优化

### 测验题目

1. **TCP和UDP的本质区别是什么？在游戏场景中如何选择？**

2. **什么是Head-of-Line Blocking？如何在游戏网络中避免？**

3. **解释以下概念：**
   - Nagle算法
   - TCP_NODELAY
   - SO_REUSEADDR
   - Keep-Alive

4. **实现题：**
   编写一个函数，计算给定网络条件下（延迟、丢包率、带宽）的理论最大吞吐量。

5. **设计题：**
   设计一个支持断线重连的会话管理系统，需要考虑：
   - 会话保持
   - 状态恢复
   - 安全性

## 📊 模块A总结

### 掌握的核心技能
✅ 理解游戏服务器的基本职责
✅ 掌握TCP/UDP的使用场景
✅ 实现基础的网络通信
✅ 理解延迟处理和时间同步
✅ 掌握消息序列化基础

### 常见问题与解决方案

| 问题 | 解决方案 |
|------|---------|
| 大量连接导致性能下降 | 使用异步I/O、连接池 |
| 消息延迟不稳定 | 实现自适应缓冲区、QoS |
| 内存泄漏 | 使用对象池、定期清理 |
| CPU使用率过高 | 批量处理、减少锁竞争 |

## 🔮 下阶段预习（模块B）

### 预习内容
1. **房间管理系统设计模式**
2. **ELO评分算法原理**
3. **帧同步vs状态同步**
4. **Interest Management（兴趣管理）**

### 推荐阅读
- 《Multiplayer Game Programming》Chapter 3-5
- Photon Engine文档
- Mirror Networking文档

### 准备工作
1. 安装Unity或Unreal Engine
2. 配置Redis环境
3. 学习基础的游戏物理概念

---

🎯 **本周作业**：基于所学内容，实现一个支持多房间的聊天服务器，要求：
- 支持创建/加入/离开房间
- 实现私聊功能
- 添加简单的权限管理（如禁言）
- 编写客户端测试工具

提交要求：
- 完整的源代码
- 架构设计文档
- 性能测试报告