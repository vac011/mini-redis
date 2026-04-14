#include "TcpServer.h"
#include "EventLoop.h"
#include "Connection.h"
#include "Store.h"
#include "ThreadPool.h"
#include "Logger.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdexcept>
#include <cstring>
#include <cerrno>

// 将 fd 设置为非阻塞
static void setNonBlock(int fd) {
    int flags = ::fcntl(fd, F_GETFL, 0);
    ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

TcpServer::TcpServer(std::string host, uint16_t port,
                     std::shared_ptr<Store>      store,
                     std::shared_ptr<ThreadPool> pool,
                     int numIoThreads)
    : host_(std::move(host)), port_(port),
      store_(std::move(store)), pool_(std::move(pool)) {
    // 创建指定数量的 EventLoop（每个独立线程）
    for (int i = 0; i < numIoThreads; ++i)
        loops_.push_back(std::make_unique<EventLoop>());
}

TcpServer::~TcpServer() {
    stop();
}

int TcpServer::createListenSocket() {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        throw std::runtime_error("socket() failed");

    // SO_REUSEADDR：快速重启服务时复用端口
    int opt = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port_);
    ::inet_pton(AF_INET, host_.c_str(), &addr.sin_addr);

    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
        throw std::runtime_error("bind() failed: " + std::string(strerror(errno)));

    if (::listen(fd, SOMAXCONN) < 0)
        throw std::runtime_error("listen() failed");

    return fd;
}

void TcpServer::start() {
    listenFd_ = createListenSocket();
    setNonBlock(listenFd_);
    running_ = true;

    LOG_INFO().log("TcpServer listening on ", host_, ":", port_,
                   " with ", loops_.size(), " IO threads");

    // 为每个 EventLoop 启动一个 IO 线程
    for (size_t i = 0; i < loops_.size(); ++i) {
        EventLoop* loop = loops_[i].get();
        ioThreads_.emplace_back([loop, i] {
            LOG_INFO().log("IO thread-", i, " started");
            loop->run();
        });
    }

    // 主 accept 线程
    acceptThread_ = std::thread([this] { acceptLoop(); });
}

void TcpServer::acceptLoop() {
    LOG_INFO().log("Accept loop started");
    while (running_) {
        sockaddr_in clientAddr{};
        socklen_t   len = sizeof(clientAddr);

        // accept4 直接设置 SOCK_NONBLOCK
        int clientFd = ::accept4(listenFd_,
                                  reinterpret_cast<sockaddr*>(&clientAddr),
                                  &len, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (clientFd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 非阻塞 accept，无新连接时短暂让出 CPU
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            if (errno == EINTR) continue;
            if (!running_) break;
            LOG_ERROR().log("accept4 error: ", std::strerror(errno));
            continue;
        }

        char ipStr[INET_ADDRSTRLEN];
        ::inet_ntop(AF_INET, &clientAddr.sin_addr, ipStr, sizeof(ipStr));
        LOG_INFO().log("New connection fd=", clientFd,
                       " from ", ipStr, ":", ntohs(clientAddr.sin_port));

        dispatchConn(clientFd);
    }
    LOG_INFO().log("Accept loop stopped");
}

void TcpServer::dispatchConn(int clientFd) {
    // 轮询选择 EventLoop（round-robin 负载均衡）
    size_t idx = nextLoop_.fetch_add(1) % loops_.size();
    auto conn  = std::make_shared<Connection>(clientFd, store_, pool_);
    loops_[idx]->addConnection(conn);
}

void TcpServer::stop() {
    if (!running_.exchange(false)) return;
    LOG_INFO().log("TcpServer stopping...");

    if (listenFd_ >= 0) {
        ::close(listenFd_);
        listenFd_ = -1;
    }

    for (auto& loop : loops_) loop->stop();

    if (acceptThread_.joinable()) acceptThread_.join();
    for (auto& t : ioThreads_)   if (t.joinable()) t.join();

    LOG_INFO().log("TcpServer stopped");
}
