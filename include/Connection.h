#pragma once
#include <string>
#include <memory>
#include <mutex>
#include <atomic>
#include <functional>

class Store;
class ThreadPool;

// Connection 代表一条客户端 TCP 连接
// 使用 shared_ptr 管理生命周期，weak_ptr 防止循环引用
// 运行时多态：Connection 是基类，可扩展 TlsConnection 等子类
class Connection : public std::enable_shared_from_this<Connection> {
public:
    using Ptr = std::shared_ptr<Connection>;
    using CloseCallback = std::function<void(int fd)>;

    Connection(int fd, std::shared_ptr<Store> store,
               std::shared_ptr<ThreadPool> pool);
    virtual ~Connection();

    // 不可拷贝
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    // 可读事件触发：从 fd 读数据并解析
    virtual void onReadable();

    // 向客户端发送响应（线程安全）
    void send(const std::string& data);

    void setCloseCallback(CloseCallback cb) { closeCallback_ = std::move(cb); }

    int  fd()      const { return fd_;    }
    bool isAlive() const { return alive_; }

protected:
    void handleError(const std::string& msg);
    void processBuffer();   // 从 readBuf_ 中解析 RESP 并执行命令
    bool writeAll(const std::string& data); // 阻塞写完（带重试）

    int                          fd_;
    std::shared_ptr<Store>       store_;
    std::shared_ptr<ThreadPool>  pool_;
    std::atomic<bool>            alive_{true};

    std::string  readBuf_;    // 读缓冲
    std::mutex   writeMu_;    // 写保护（多线程并发写响应）
    CloseCallback closeCallback_;
};
