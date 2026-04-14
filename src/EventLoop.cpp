#include "EventLoop.h"
#include "Connection.h"
#include "Logger.h"
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <stdexcept>
#include <cstring>
#include <cerrno>

EventLoop::EventLoop() {
    // 创建 epoll 实例
    epollFd_ = ::epoll_create1(EPOLL_CLOEXEC);
    if (epollFd_ < 0)
        throw std::runtime_error("epoll_create1 failed");

    // eventfd：用于唤醒阻塞中的 epoll_wait（如 stop() 时）
    wakeupFd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (wakeupFd_ < 0)
        throw std::runtime_error("eventfd failed");

    // 注册唤醒 fd
    epoll_event ev{};
    ev.events  = EPOLLIN;
    ev.data.fd = wakeupFd_;
    ::epoll_ctl(epollFd_, EPOLL_CTL_ADD, wakeupFd_, &ev);
}

EventLoop::~EventLoop() {
    if (epollFd_ >= 0) ::close(epollFd_);
    if (wakeupFd_ >= 0) ::close(wakeupFd_);
}

void EventLoop::addConnection(std::shared_ptr<Connection> conn) {
    int fd = conn->fd();

    // 设置关闭回调
    conn->setCloseCallback([this](int cfd) { removeConnection(cfd); });

    {
        std::lock_guard<std::mutex> lock(connsMu_);
        conns_[fd] = conn;
    }

    // 注册 EPOLLIN | EPOLLET（边缘触发，非阻塞读）
    epoll_event ev{};
    ev.events  = EPOLLIN | EPOLLET | EPOLLRDHUP;
    ev.data.fd = fd;
    if (::epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &ev) < 0) {
        LOG_ERROR().log("epoll_ctl ADD fd=", fd, " err=", std::strerror(errno));
        std::lock_guard<std::mutex> lock(connsMu_);
        conns_.erase(fd);
    }
}

void EventLoop::removeConnection(int fd) {
    ::epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, nullptr);
    std::lock_guard<std::mutex> lock(connsMu_);
    conns_.erase(fd);
    LOG_DEBUG().log("EventLoop removed fd=", fd,
                    ", total conns=", conns_.size());
}

void EventLoop::run() {
    running_ = true;
    LOG_INFO().log("EventLoop started (epollFd=", epollFd_, ")");

    epoll_event events[kMaxEvents];

    while (running_) {
        // 多路 I/O 复用核心：epoll_wait 阻塞等待事件
        int nfds = ::epoll_wait(epollFd_, events, kMaxEvents, 200 /*ms*/);
        if (nfds < 0) {
            if (errno == EINTR) continue;
            LOG_ERROR().log("epoll_wait error: ", std::strerror(errno));
            break;
        }

        for (int i = 0; i < nfds; ++i) {
            int      fd  = events[i].data.fd;
            uint32_t ev  = events[i].events;

            if (fd == wakeupFd_) {
                // 消费唤醒事件
                uint64_t val;
                ::read(wakeupFd_, &val, sizeof(val));
                continue;
            }

            handleEvent(fd, ev);
        }
    }

    LOG_INFO().log("EventLoop stopped");
}

void EventLoop::handleEvent(int fd, uint32_t events) {
    std::shared_ptr<Connection> conn;
    {
        std::lock_guard<std::mutex> lock(connsMu_);
        auto it = conns_.find(fd);
        if (it == conns_.end()) return;
        conn = it->second;
    }

    // 对端关闭 或 错误
    if (events & (EPOLLRDHUP | EPOLLERR | EPOLLHUP)) {
        LOG_INFO().log("EventLoop: fd=", fd, " hangup/error, removing");
        removeConnection(fd);
        return;
    }

    // 可读（非阻塞 ET 模式）
    if (events & EPOLLIN) {
        conn->onReadable();
        if (!conn->isAlive()) {
            removeConnection(fd);
        }
    }
}

void EventLoop::stop() {
    running_ = false;
    // 写 eventfd 唤醒阻塞的 epoll_wait
    uint64_t val = 1;
    ::write(wakeupFd_, &val, sizeof(val));
}

size_t EventLoop::connectionCount() const {
    std::lock_guard<std::mutex> lock(connsMu_);
    return conns_.size();
}
