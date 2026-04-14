#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <chrono>
#include <atomic>

// 阻塞式线程安全队列：
//   push() 在队满时阻塞（有界队列）
//   pop()  在队空时阻塞
// 体现"阻塞式 I/O"的核心等待语义
template<typename T>
class BlockingQueue {
public:
    explicit BlockingQueue(size_t cap = 1024) : cap_(cap), closed_(false) {}

    // ---- 生产者 --------------------------------------------------------
    // 阻塞直到有空位或队列关闭
    bool push(T val) {
        std::unique_lock<std::mutex> lock(mu_);
        notFull_.wait(lock, [this] { return queue_.size() < cap_ || closed_; });
        if (closed_) return false;
        queue_.push(std::move(val));
        notEmpty_.notify_one();
        return true;
    }

    // 非阻塞尝试入队
    bool tryPush(T val) {
        std::lock_guard<std::mutex> lock(mu_);
        if (closed_ || queue_.size() >= cap_) return false;
        queue_.push(std::move(val));
        notEmpty_.notify_one();
        return true;
    }

    // ---- 消费者 --------------------------------------------------------
    // 阻塞直到有元素或队列关闭
    std::optional<T> pop() {
        std::unique_lock<std::mutex> lock(mu_);
        notEmpty_.wait(lock, [this] { return !queue_.empty() || closed_; });
        if (queue_.empty()) return std::nullopt; // 已关闭且为空
        auto val = std::move(queue_.front());
        queue_.pop();
        notFull_.notify_one();
        return val;
    }

    // 超时等待
    std::optional<T> popFor(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mu_);
        if (!notEmpty_.wait_for(lock, timeout,
                [this] { return !queue_.empty() || closed_; }))
            return std::nullopt;
        if (queue_.empty()) return std::nullopt;
        auto val = std::move(queue_.front());
        queue_.pop();
        notFull_.notify_one();
        return val;
    }

    // ---- 管理 ----------------------------------------------------------
    void close() {
        std::lock_guard<std::mutex> lock(mu_);
        closed_ = true;
        notEmpty_.notify_all();
        notFull_.notify_all();
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mu_);
        return queue_.empty();
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mu_);
        return queue_.size();
    }

private:
    std::queue<T>           queue_;
    mutable std::mutex      mu_;
    std::condition_variable notFull_;   // 队不满时通知
    std::condition_variable notEmpty_;  // 队不空时通知
    size_t                  cap_;
    bool                    closed_;
};
