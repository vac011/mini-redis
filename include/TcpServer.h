#pragma once
#include <string>
#include <memory>
#include <thread>
#include <vector>
#include <atomic>

class Store;
class ThreadPool;
class EventLoop;

// TcpServer：监听端口，accept 新连接，分发给 EventLoop
// 多线程：主线程 accept，IO 线程运行 EventLoop
class TcpServer {
public:
    TcpServer(std::string host, uint16_t port,
              std::shared_ptr<Store>      store,
              std::shared_ptr<ThreadPool> pool,
              int numIoThreads = 2);
    ~TcpServer();

    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;

    // 启动服务（创建 IO 线程，开始 accept 循环）
    void start();

    // 停止服务
    void stop();

private:
    int  createListenSocket();          // 创建并绑定监听 socket
    void acceptLoop();                  // accept 主循环
    void dispatchConn(int clientFd);    // 轮询分发到 EventLoop

    std::string                          host_;
    uint16_t                             port_;
    std::shared_ptr<Store>               store_;
    std::shared_ptr<ThreadPool>          pool_;

    int                                  listenFd_{-1};
    std::atomic<bool>                    running_{false};

    // 多个 EventLoop（每个跑在独立线程里）
    std::vector<std::unique_ptr<EventLoop>> loops_;
    std::vector<std::thread>                ioThreads_;
    std::thread                             acceptThread_;
    std::atomic<size_t>                     nextLoop_{0};  // round-robin
};
