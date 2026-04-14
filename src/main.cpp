#include "Store.h"
#include "ThreadPool.h"
#include "TcpServer.h"
#include "Logger.h"
#include <memory>
#include <thread>
#include <chrono>
#include <csignal>
#include <atomic>

// 信号处理：优雅退出
static std::atomic<bool> g_shutdown{false};

static void onSignal(int) {
    g_shutdown = true;
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT,  onSignal);
    std::signal(SIGTERM, onSignal);

    // ---- 配置 ----
    std::string host       = "127.0.0.1";
    uint16_t    port       = 6399;
    int         ioThreads  = 2;
    int         poolThreads = 4;

    if (argc >= 3) {
        host = argv[1];
        port = static_cast<uint16_t>(std::stoi(argv[2]));
    }

    LOG_INFO().log("=== mini-redis starting ===");
    LOG_INFO().log("Host=", host, " Port=", port,
                   " IO threads=", ioThreads,
                   " Worker threads=", poolThreads);

    // ---- 依赖注入：shared_ptr 统一管理生命周期 ----
    auto store = std::make_shared<MemoryStore>();
    auto pool  = std::make_shared<ThreadPool>(poolThreads);

    TcpServer server(host, port, store, pool, ioThreads);
    server.start();

    // ---- 后台任务：定期清理过期 key ----
    std::thread evictThread([&store] {
        while (!g_shutdown) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            int n = store->evictExpired();
            if (n > 0)
                LOG_INFO().log("Evicted ", n, " expired keys");
        }
    });

    // ---- 等待退出信号 ----
    while (!g_shutdown) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    LOG_INFO().log("Shutting down...");
    server.stop();
    pool->shutdown();
    evictThread.join();

    LOG_INFO().log("=== mini-redis stopped ===");
    return 0;
}
