#pragma once
#include "BlockingQueue.h"
#include "Logger.h"
#include <vector>
#include <thread>
#include <functional>
#include <memory>
#include <atomic>

// 多线程并发：固定大小线程池
// 使用 BlockingQueue 实现任务分发
class ThreadPool {
public:
    using Task = std::function<void()>;

    explicit ThreadPool(size_t numThreads = std::thread::hardware_concurrency())
        : running_(true) {
        LOG_INFO().log("ThreadPool starting with ", numThreads, " threads");
        for (size_t i = 0; i < numThreads; ++i) {
            workers_.emplace_back([this, i] { workerLoop(i); });
        }
    }

    ~ThreadPool() {
        shutdown();
    }

    // 提交任务到队列
    bool submit(Task task) {
        if (!running_) return false;
        return taskQueue_.push(std::move(task));
    }

    // 优雅关闭
    void shutdown() {
        if (!running_.exchange(false)) return;
        LOG_INFO().log("ThreadPool shutting down...");
        taskQueue_.close();
        for (auto& t : workers_) {
            if (t.joinable()) t.join();
        }
        LOG_INFO().log("ThreadPool stopped");
    }

    size_t queueSize() const { return taskQueue_.size(); }

private:
    void workerLoop(size_t id) {
        LOG_DEBUG().log("Worker-", id, " started");
        while (running_) {
            auto task = taskQueue_.pop();
            if (!task) break; // 队列已关闭
            try {
                (*task)();
            } catch (const std::exception& e) {
                LOG_ERROR().log("Worker-", id, " exception: ", e.what());
            }
        }
        LOG_DEBUG().log("Worker-", id, " exited");
    }

    std::vector<std::thread> workers_;
    BlockingQueue<Task>      taskQueue_;
    std::atomic<bool>        running_;
};
