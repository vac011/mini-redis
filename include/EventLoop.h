#pragma once
#include <functional>
#include <unordered_map>
#include <memory>
#include <atomic>
#include <mutex>

class Connection;

// EventLoop：基于 epoll 的 I/O 事件循环
// 体现：多路 I/O 复用、非阻塞 IO
class EventLoop {
public:
    using ConnMap = std::unordered_map<int, std::shared_ptr<Connection>>;

    EventLoop();
    ~EventLoop();

    // 不可拷贝/移动
    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    // 向 epoll 注册连接（EPOLLIN | EPOLLET）
    void addConnection(std::shared_ptr<Connection> conn);

    // 移除连接
    void removeConnection(int fd);

    // 启动事件循环（阻塞直到 stop()）
    void run();

    // 通知停止
    void stop();

    size_t connectionCount() const;

private:
    void handleEvent(int fd, uint32_t events);

    int       epollFd_{-1};
    int       wakeupFd_{-1};    // eventfd，用于唤醒 epoll_wait
    std::atomic<bool> running_{false};
    ConnMap   conns_;           // fd → Connection
    mutable   std::mutex connsMu_;

    static constexpr int kMaxEvents = 1024;
};
