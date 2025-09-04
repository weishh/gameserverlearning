#pragma once

#include <atomic>
#include <functional>
#include <vector>
#include <memory>
#include <mutex>
#include <thread>

namespace gameserver {

class Channel;
class Poller;
class TimerQueue;

// Reactor模式的核心：事件循环
// 每个EventLoop对象都必须在其所属的线程中创建和运行
class EventLoop {
public:
    using Functor = std::function<void()>;
    
    EventLoop();
    ~EventLoop();
    
    // 禁止拷贝
    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;
    
    // 开始事件循环，必须在创建EventLoop的线程中调用
    void Loop();
    
    // 退出事件循环
    void Quit();
    
    // 在loop线程中执行回调
    void RunInLoop(Functor cb);
    
    // 将回调加入队列，并在必要时唤醒loop线程执行
    void QueueInLoop(Functor cb);
    
    // 定时器接口
    // run_at: 在指定时间点执行
    // run_after: 在指定延迟后执行
    // run_every: 每隔指定时间执行一次
    using TimerId = int64_t;
    TimerId RunAt(double timestamp, Functor cb);
    TimerId RunAfter(double delay, Functor cb);
    TimerId RunEvery(double interval, Functor cb);
    void CancelTimer(TimerId timer_id);
    
    // 唤醒事件循环
    void Wakeup();
    
    // 更新Channel
    void UpdateChannel(Channel* channel);
    void RemoveChannel(Channel* channel);
    bool HasChannel(Channel* channel) const;
    
    // 断言在loop线程中
    void AssertInLoopThread() {
        if (!IsInLoopThread()) {
            AbortNotInLoopThread();
        }
    }
    
    bool IsInLoopThread() const {
        return thread_id_ == std::this_thread::get_id();
    }
    
    // 获取当前线程的EventLoop对象
    static EventLoop* GetEventLoopOfCurrentThread();
    
private:
    void AbortNotInLoopThread();
    void HandleRead();  // 处理wakeupFd_上的读事件，用于唤醒
    void DoPendingFunctors();
    
private:
    using ChannelList = std::vector<Channel*>;
    
    std::atomic<bool> looping_;
    std::atomic<bool> quit_;
    std::atomic<bool> calling_pending_functors_;
    const std::thread::id thread_id_;
    
    std::unique_ptr<Poller> poller_;
    std::unique_ptr<TimerQueue> timer_queue_;
    
    int wakeup_fd_;
    std::unique_ptr<Channel> wakeup_channel_;
    
    ChannelList active_channels_;
    Channel* current_active_channel_;
    
    mutable std::mutex mutex_;
    std::vector<Functor> pending_functors_;  // @GuardedBy mutex_
};

} // namespace gameserver