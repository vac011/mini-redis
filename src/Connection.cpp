#include "Connection.h"
#include "Store.h"
#include "ThreadPool.h"
#include "Command.h"
#include "RespCodec.h"
#include "Logger.h"
#include <unistd.h>
#include <sys/socket.h>
#include <cerrno>
#include <cstring>

static constexpr size_t kReadBufSize = 4096;

Connection::Connection(int fd,
                       std::shared_ptr<Store>      store,
                       std::shared_ptr<ThreadPool> pool)
    : fd_(fd), store_(std::move(store)), pool_(std::move(pool)) {}

Connection::~Connection() {
    if (fd_ >= 0) ::close(fd_);
}

// 非阻塞读：epoll 通知可读后调用
void Connection::onReadable() {
    char buf[kReadBufSize];
    while (true) {
        ssize_t n = ::recv(fd_, buf, sizeof(buf), 0);
        if (n > 0) {
            readBuf_.append(buf, n);
            // 继续尝试读（ET 模式必须读完）
        } else if (n == 0) {
            // 对端关闭连接
            LOG_INFO().log("Connection fd=", fd_, " closed by peer");
            alive_ = false;
            if (closeCallback_) closeCallback_(fd_);
            return;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 数据读完，退出循环
                break;
            }
            // 真实错误
            handleError(std::strerror(errno));
            return;
        }
    }
    // 解析并执行命令
    processBuffer();
}

void Connection::processBuffer() {
    // 循环解析，一次可能有多条 RESP 命令
    while (!readBuf_.empty()) {
        auto result = RespCodec::decode(readBuf_.data(), readBuf_.size());

        if (!result.ok) {
            // 数据不完整，等待更多数据
            break;
        }

        // 消费已解析的字节
        readBuf_.erase(0, result.consumed);

        // 只处理数组类型的命令（RESP 命令格式）
        if (result.value.type != RespType::Array || result.value.elements.empty()) {
            send(RespCodec::encode(RespValue::error("ERR invalid command format")));
            continue;
        }

        // 提取字符串参数
        std::vector<std::string> args;
        for (auto& elem : result.value.elements) {
            if (elem.type == RespType::BulkString)
                args.push_back(elem.str);
        }

        // 构造命令对象（工厂方法 + 运行时多态）
        auto cmd = Command::create(args);
        if (!cmd) {
            std::string errMsg = "ERR unknown command";
            if (!args.empty()) errMsg += " '" + args[0] + "'";
            send(RespCodec::encode(RespValue::error(errMsg)));
            continue;
        }

        // 提交到线程池异步执行
        // 使用 shared_ptr 确保 Connection 存活到任务完成
        auto self = shared_from_this();
        auto storePtr = store_;
        // 通过 unique_ptr 转移命令所有权进 lambda
        auto* rawCmd = cmd.release();
        pool_->submit([self, storePtr, rawCmd]() {
            std::unique_ptr<Command> ownedCmd(rawCmd);
            std::string response = ownedCmd->execute(*storePtr);
            if (self->isAlive())
                self->send(response);
        });
    }
}

// 线程安全发送（线程池中调用）
void Connection::send(const std::string& data) {
    std::lock_guard<std::mutex> lock(writeMu_);
    if (!alive_) return;
    writeAll(data);
}

// 阻塞写完（非阻塞 fd 需要循环写）
bool Connection::writeAll(const std::string& data) {
    size_t total = 0;
    while (total < data.size()) {
        ssize_t n = ::write(fd_, data.data() + total, data.size() - total);
        if (n < 0) {
            if (errno == EINTR) continue;
            LOG_ERROR().log("write fd=", fd_, " err=", std::strerror(errno));
            alive_ = false;
            return false;
        }
        total += n;
    }
    return true;
}

void Connection::handleError(const std::string& msg) {
    LOG_ERROR().log("Connection fd=", fd_, " error: ", msg);
    alive_ = false;
    if (closeCallback_) closeCallback_(fd_);
}
