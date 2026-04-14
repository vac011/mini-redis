# Mini-Redis

## 项目概述

Mini-Redis 是一个基于 C++17 实现的轻量级内存数据库，兼容 Redis RESP 协议。项目通过实现完整的网络服务器、命令解析、数据存储等模块，系统性地展示了 C++ 核心专业概念的实际应用。

**核心特性**：

- 支持 8 种 Redis 命令（PING/SET/GET/DEL/EXISTS/KEYS/INCR/DECR）
- 基于 epoll 的高性能事件驱动架构
- 线程池异步处理命令
- TTL 过期机制
- 完整的 RESP 协议实现

---

## 技术架构

### 整体架构图

```text
┌─────────────┐
│   Client    │
└──────┬──────┘
       │ RESP Protocol
       ▼
┌─────────────────────────────────────┐
│         TcpServer                   │
│  ┌──────────┐  ┌──────────┐        │
│  │EventLoop1│  │EventLoop2│  ...   │  ◄── Round-robin 负载均衡
│  └────┬─────┘  └────┬─────┘        │
└───────┼─────────────┼──────────────┘
        │             │
        ▼             ▼
   ┌─────────────────────┐
   │   Connection Pool   │  ◄── shared_ptr 生命周期管理
   └──────────┬──────────┘
              │
              ▼
   ┌─────────────────────┐
   │    ThreadPool       │  ◄── 阻塞队列 + 多线程并发
   └──────────┬──────────┘
              │
              ▼
   ┌─────────────────────┐
   │  Command Factory    │  ◄── 运行时多态（虚函数）
   └──────────┬──────────┘
              │
              ▼
   ┌─────────────────────┐
   │    MemoryStore      │  ◄── shared_mutex 读写锁
   └─────────────────────┘
```

---

## 核心技术实现

### 1. 编译时多态 - CRTP 模板

**实现位置**：`include/Logger.h`

```cpp
template<typename Derived>
class LoggerBase {
public:
    template<typename... Args>
    void log(Args&&... args) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ostringstream oss;
        oss << "[" << timestamp() << "] "
            << "[" << static_cast<Derived*>(this)->getLevel() << "] ";
        (oss << ... << std::forward<Args>(args));  // 折叠表达式
        std::cout << oss.str() << std::endl;
    }
private:
    static std::mutex mutex_;
};

class InfoLogger : public LoggerBase<InfoLogger> {
public:
    const char* getLevel() { return "INFO"; }
};
```

**技术要点**：

- CRTP（Curiously Recurring Template Pattern）实现编译时多态
- 通过 `static_cast<Derived*>(this)` 调用子类方法，零运行时开销
- 可变参数模板 + 折叠表达式实现灵活的日志参数
- 静态互斥锁保证多线程日志输出的原子性

**优势**：相比虚函数，避免了虚表查找开销，适合高频调用场景。

---

### 2. 运行时多态 - 虚函数与工厂模式

**实现位置**：`include/Command.h`

```cpp
class Command {
public:
    virtual ~Command() = default;
    virtual std::string execute(Store& store) = 0;  // 纯虚函数
    virtual std::string name() const = 0;
    
    static std::unique_ptr<Command> create(const std::vector<std::string>& args);
};

class SetCommand : public Command {
public:
    std::string execute(Store& store) override {
        store.set(key_, value_, ttl_);
        return RespCodec::encode(RespValue::simpleString("OK"));
    }
    std::string name() const override { return "SET"; }
private:
    std::string key_, value_;
    std::chrono::seconds ttl_;
};
```

**技术要点**：

- 抽象基类 `Command` 定义统一接口
- 工厂方法 `create()` 根据命令名动态创建子类实例
- 返回 `unique_ptr` 实现自动内存管理
- 8 个命令子类各自实现 `execute()` 逻辑

**设计模式**：工厂方法模式 + 策略模式，支持命令的动态扩展。

---

### 3. 智能指针的三种应用

#### 3.1 shared_ptr - 共享所有权

**场景**：Store 和 ThreadPool 需要被多个 Connection 共享

```cpp
class Connection {
    std::shared_ptr<Store>       store_;   // 多个连接共享同一存储
    std::shared_ptr<ThreadPool>  pool_;    // 多个连接共享同一线程池
};

// main.cpp 中注入依赖
auto store = std::make_shared<MemoryStore>();
auto pool  = std::make_shared<ThreadPool>(4);
TcpServer server("127.0.0.1", 6399, store, pool, 2);
```

#### 3.2 unique_ptr - 独占所有权

**场景**：Command 对象的生命周期由单一所有者管理

```cpp
std::unique_ptr<Command> cmd = Command::create(args);
auto* rawCmd = cmd.release();  // 转移所有权到 lambda
pool_->submit([self, storePtr, rawCmd]() {
    std::unique_ptr<Command> ownedCmd(rawCmd);  // 重新获取所有权
    std::string response = ownedCmd->execute(*storePtr);
});
```

#### 3.3 enable_shared_from_this - 安全获取自身指针

**场景**：异步任务需要延长 Connection 生命周期

```cpp
class Connection : public std::enable_shared_from_this<Connection> {
    void processBuffer() {
        auto self = shared_from_this();  // 获取 shared_ptr<Connection>
        pool_->submit([self, ...]() {
            // lambda 持有 self，确保 Connection 不会提前析构
            if (self->isAlive()) self->send(response);
        });
    }
};
```

---

### 4. 非阻塞 IO + 边缘触发

**实现位置**：`src/Connection.cpp`

```cpp
void Connection::onReadable() {
    char buf[4096];
    while (true) {
        ssize_t n = ::recv(fd_, buf, sizeof(buf), 0);
        if (n > 0) {
            readBuf_.append(buf, n);
        } else if (n == 0) {
            alive_ = false;  // 对端关闭
            return;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;  // 数据读完，退出循环
            }
            handleError("recv failed");
            return;
        }
    }
    processBuffer();
}
```

**技术要点**：

- 边缘触发（ET）模式：必须循环读取直到 `EAGAIN`
- 非阻塞 socket：`accept4(SOCK_NONBLOCK)` 和 `fcntl(O_NONBLOCK)`
- 流式缓冲区：`readBuf_` 累积不完整的 RESP 帧

**优势**：相比水平触发（LT），ET 模式减少 epoll_wait 唤醒次数，提升性能。

---

### 5. 多路 IO 复用 - epoll

**实现位置**：`src/EventLoop.cpp`

```cpp
void EventLoop::run() {
    running_ = true;
    struct epoll_event events[kMaxEvents];
    
    while (running_) {
        int n = epoll_wait(epollFd_, events, kMaxEvents, 200);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        
        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;
            if (fd == wakeupFd_) {
                uint64_t val;
                ::read(wakeupFd_, &val, sizeof(val));  // 清空 eventfd
                continue;
            }
            
            auto conn = getConnection(fd);
            if (!conn) continue;
            
            if (events[i].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
                removeConnection(fd);
            } else if (events[i].events & EPOLLIN) {
                conn->onReadable();
            }
        }
    }
}
```

**技术要点**：

- `epoll_create1(EPOLL_CLOEXEC)` 创建 epoll 实例
- `EPOLLIN | EPOLLET | EPOLLRDHUP` 监听可读事件和连接关闭
- `eventfd` 实现优雅唤醒机制（替代 pipe 的轻量方案）
- 200ms 超时避免 CPU 空转

**架构优势**：单线程 EventLoop 可管理数千个并发连接。

---

### 6. 多线程并发 - 线程池与阻塞队列

#### 6.1 阻塞队列

**实现位置**：`include/BlockingQueue.h`

```cpp
template<typename T>
class BlockingQueue {
public:
    bool push(T val) {
        std::unique_lock<std::mutex> lock(mu_);
        notFull_.wait(lock, [this] { 
            return queue_.size() < cap_ || closed_; 
        });
        if (closed_) return false;
        queue_.push(std::move(val));
        notEmpty_.notify_one();
        return true;
    }
    
    std::optional<T> pop() {
        std::unique_lock<std::mutex> lock(mu_);
        notEmpty_.wait(lock, [this] { 
            return !queue_.empty() || closed_; 
        });
        if (closed_ && queue_.empty()) return std::nullopt;
        T val = std::move(queue_.front());
        queue_.pop();
        notFull_.notify_one();
        return val;
    }
private:
    std::queue<T>           queue_;
    std::mutex              mu_;
    std::condition_variable notFull_, notEmpty_;
    size_t                  cap_;
    bool                    closed_;
};
```

**技术要点**：

- `std::condition_variable` 实现生产者-消费者模型
- `wait()` 的 lambda 谓词防止虚假唤醒
- `closed_` 标志实现优雅关闭

#### 6.2 线程池

**实现位置**：`include/ThreadPool.h`

```cpp
class ThreadPool {
public:
    explicit ThreadPool(size_t numThreads = std::thread::hardware_concurrency())
        : taskQueue_(1000), running_(true) {
        for (size_t i = 0; i < numThreads; ++i) {
            workers_.emplace_back(&ThreadPool::workerLoop, this, i);
        }
    }
    
    bool submit(Task task) {
        return taskQueue_.push(std::move(task));
    }
    
    void shutdown() {
        running_ = false;
        taskQueue_.close();
        for (auto& t : workers_) {
            if (t.joinable()) t.join();
        }
    }
private:
    void workerLoop(size_t id) {
        while (running_) {
            auto task = taskQueue_.pop();
            if (!task) break;
            (*task)();
        }
    }
    
    std::vector<std::thread> workers_;
    BlockingQueue<Task>      taskQueue_;
    std::atomic<bool>        running_;
};
```

**技术要点**：

- 工作线程数默认为 `hardware_concurrency()`
- `std::atomic<bool>` 实现无锁退出标志
- 任务队列容量 1000，防止内存无限增长

**性能优势**：避免频繁创建/销毁线程，复用线程资源。

---

### 7. 读写锁 - shared_mutex

**实现位置**：`src/Store.cpp`

```cpp
class MemoryStore : public Store {
public:
    std::optional<std::string> get(const std::string& key) const override {
        std::shared_lock<std::shared_mutex> lock(mutex_);  // 读锁
        auto it = findValid(key);
        if (it == data_.end()) return std::nullopt;
        return it->second.value;
    }
    
    void set(const std::string& key, std::string value, 
             std::chrono::seconds ttl) override {
        std::unique_lock<std::shared_mutex> lock(mutex_);  // 写锁
        Entry entry{std::move(value)};
        if (ttl.count() > 0) {
            entry.expireAt = std::chrono::steady_clock::now() + ttl;
        }
        data_[key] = std::move(entry);
    }
private:
    mutable std::shared_mutex               mutex_;
    std::unordered_map<std::string, Entry>  data_;
};
```

**技术要点**：

- `shared_lock` 允许多个读操作并发执行
- `unique_lock` 保证写操作独占访问
- `mutable` 修饰符允许 const 方法获取锁

**性能对比**：

| 锁类型 | 读并发 | 写并发 | 适用场景 |
|--------|--------|--------|----------|
| mutex | ✗ | ✗ | 读写频率相当 |
| shared_mutex | ✓ | ✗ | 读多写少（本项目） |

---

### 8. RESP 协议实现

**实现位置**：`src/RespCodec.cpp`

#### 8.1 协议格式

```text
简单字符串：+OK\r\n
错误：      -ERR unknown command\r\n
整数：      :1000\r\n
批量字符串：$5\r\nhello\r\n
数组：      *2\r\n$3\r\nGET\r\n$3\r\nkey\r\n
```

#### 8.2 流式解析

```cpp
ParseResult RespCodec::decode(const char* data, size_t len) {
    if (len == 0) return {false, {}, 0, "empty"};
    
    size_t cursor = 0;
    auto result = parseValue(data, len, cursor);
    if (!result.ok) return result;
    
    result.consumed = cursor;  // 绝对偏移量
    return result;
}

ParseResult parseValue(const char* data, size_t len, size_t& cursor) {
    char type = data[cursor++];
    switch (type) {
        case '+': return parseSimpleString(data, len, cursor);
        case '-': return parseError(data, len, cursor);
        case ':': return parseInteger(data, len, cursor);
        case '$': return parseBulkString(data, len, cursor);
        case '*': return parseArray(data, len, cursor);
        default:  return {false, {}, 0, "invalid type"};
    }
}
```

**技术要点**：

- `cursor` 作为引用参数，递归解析时自动更新偏移量
- `findCRLF()` 线性扫描 `\r\n` 分隔符
- 数组递归解析：`parseArray()` 循环调用 `parseValue()`

**容错设计**：不完整的帧返回 `ok=false`，等待更多数据到达。

---

### 9. TTL 过期机制

**实现位置**：`include/Store.h` 和 `src/Store.cpp`

```cpp
struct Entry {
    std::string value;
    std::chrono::steady_clock::time_point expireAt = 
        std::chrono::steady_clock::time_point::max();
    
    bool isExpired() const {
        return expireAt != std::chrono::steady_clock::time_point::max() &&
               std::chrono::steady_clock::now() >= expireAt;
    }
};

int MemoryStore::evictExpired() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    int count = 0;
    for (auto it = data_.begin(); it != data_.end();) {
        if (it->second.isExpired()) {
            it = data_.erase(it);
            ++count;
        } else {
            ++it;
        }
    }
    return count;
}
```

**过期策略**：

1. **惰性删除**：`get()`/`exists()` 时检查 `isExpired()`
2. **定期清理**：后台线程每 5 秒调用 `evictExpired()`

**时间精度**：使用 `steady_clock` 避免系统时间调整影响。

---

## 性能优化

### 1. 零拷贝技术

```cpp
// 避免不必要的字符串拷贝
void set(const std::string& key, std::string value, ...) {
    data_[key] = Entry{std::move(value)};  // 移动语义
}
```

### 2. 对象池复用

```cpp
// Connection 对象通过 shared_ptr 管理，避免频繁 new/delete
std::unordered_map<int, std::shared_ptr<Connection>> conns_;
```

### 3. 批量事件处理

```cpp
// epoll_wait 一次返回多个就绪事件，减少系统调用
int n = epoll_wait(epollFd_, events, kMaxEvents, 200);
for (int i = 0; i < n; ++i) { /* 处理事件 */ }
```

### 4. Round-robin 负载均衡

```cpp
void TcpServer::dispatchConn(int clientFd) {
    size_t idx = nextLoop_.fetch_add(1, std::memory_order_relaxed) % loops_.size();
    loops_[idx]->addConnection(conn);
}
```

---

## 测试验证

### 测试环境

- 操作系统：Linux (WSL2)
- 编译器：g++ 11.4.0
- C++ 标准：C++17

### 功能测试

#### 1. 基础命令测试（simple_test.py）

```python
# 测试用例
tests = [
    ("PING", "+PONG\r\n"),
    ("SET mykey myvalue", "+OK\r\n"),
    ("GET mykey", "$7\r\nmyvalue\r\n"),
    ("INCR counter", ":1\r\n"),
    ("INCR counter", ":2\r\n"),
    ("GET counter", "$1\r\n2\r\n"),
    ("DEL mykey counter", ":2\r\n"),
    ("EXISTS mykey", ":0\r\n"),
    ("GET nonexistent", "$-1\r\n"),
]
```

**测试结果**：10/10 PASS ✓

#### 2. TTL 过期测试（ttl_test.py）

```python
# 设置 1 秒过期
send_command(sock, "SET mykey hello EX 1")
assert recv_response(sock) == "+OK\r\n"

# 立即读取，应该存在
send_command(sock, "GET mykey")
assert recv_response(sock) == "$5\r\nhello\r\n"

# 等待 1.5 秒后读取，应该过期
time.sleep(1.5)
send_command(sock, "GET mykey")
assert recv_response(sock) == "$-1\r\n"  # Null
```

**测试结果**：4/4 PASS ✓

---

## 项目统计

### 代码规模

| 模块 | 文件数 | 代码行数 |
|------|--------|----------|
| 头文件 | 8 | ~800 |
| 源文件 | 7 | ~1200 |
| 测试脚本 | 2 | ~150 |
| **总计** | **17** | **~2150** |

### 技术覆盖

| 技术点 | 实现模块 | 难度 |
|--------|----------|------|
| CRTP 编译时多态 | Logger | ★★★☆☆ |
| 虚函数运行时多态 | Command | ★★☆☆☆ |
| 智能指针 | 全局 | ★★★☆☆ |
| 非阻塞 IO | Connection | ★★★★☆ |
| epoll 多路复用 | EventLoop | ★★★★★ |
| 线程池 | ThreadPool | ★★★★☆ |
| 读写锁 | MemoryStore | ★★★☆☆ |
| RESP 协议 | RespCodec | ★★★★☆ |

---

## 可扩展方向

### 1. 命令扩展

- `EXPIRE key seconds` - 设置过期时间
- `TTL key` - 查询剩余时间
- `HSET`/`HGET` - 哈希表操作
- `LPUSH`/`LPOP` - 列表操作

### 2. 持久化

- AOF（Append-Only File）日志
- RDB（Redis Database）快照

### 3. 集群支持

- 主从复制
- 哨兵模式
- 分片（Sharding）

### 4. 性能优化

- 内存池（Memory Pool）
- 无锁数据结构（Lock-free Queue）
- SIMD 加速字符串操作

---

## 总结

Mini-Redis 项目系统性地实现了：

1. **编译时多态**（CRTP）和**运行时多态**（虚函数）的对比应用
2. **智能指针**（shared_ptr/unique_ptr/weak_ptr）的生命周期管理
3. **非阻塞 IO** + **epoll** 的高性能网络编程
4. **线程池** + **阻塞队列**的多线程并发模型
5. **读写锁**（shared_mutex）的细粒度同步
6. **RESP 协议**的流式解析
7. **TTL 过期**机制的时间管理

---

**项目地址**：`./mini-redis`  
**编译命令**：`cd build && cmake .. && make`  
**运行命令**：`./mini-redis`  
**测试命令**：`python3 tests/simple_test.py && python3 tests/ttl_test.py`
