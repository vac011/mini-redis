#pragma once
#include <string>
#include <vector>
#include <optional>
#include <chrono>
#include <unordered_map>
#include <shared_mutex>
#include <memory>

// 存储值的类型枚举
enum class ValueType { String };

// Store 抽象接口：运行时多态
// 将来可以扩展为 PersistentStore、ClusterStore 等
class Store {
public:
    virtual ~Store() = default;

    virtual std::optional<std::string> get(const std::string& key) const = 0;
    virtual void set(const std::string& key, std::string value,
                     std::chrono::seconds ttl = std::chrono::seconds(-1)) = 0;
    virtual int  del(const std::vector<std::string>& keys) = 0;
    virtual bool exists(const std::string& key) const = 0;
    virtual std::vector<std::string> keys(const std::string& pattern) const = 0;

    // 原子自增自减
    virtual std::optional<long long> incr(const std::string& key, long long delta = 1) = 0;

    // 过期清理（主动调用）
    virtual int evictExpired() = 0;
};

// 存储条目（带 TTL）
struct Entry {
    std::string value;
    // 过期时间点，若 == time_point::max() 表示永不过期
    std::chrono::steady_clock::time_point expireAt
        = std::chrono::steady_clock::time_point::max();

    bool isExpired() const {
        return expireAt != std::chrono::steady_clock::time_point::max()
            && std::chrono::steady_clock::now() > expireAt;
    }
};

// MemoryStore：基于 unordered_map 的内存实现
// 使用 shared_mutex 实现读写分离（多读单写）
class MemoryStore : public Store {
public:
    explicit MemoryStore() = default;

    std::optional<std::string> get(const std::string& key) const override;
    void set(const std::string& key, std::string value,
             std::chrono::seconds ttl = std::chrono::seconds(-1)) override;
    int  del(const std::vector<std::string>& keys) override;
    bool exists(const std::string& key) const override;
    std::vector<std::string> keys(const std::string& pattern) const override;
    std::optional<long long> incr(const std::string& key, long long delta = 1) override;
    int evictExpired() override;

private:
    // 内部不加锁版本，供已加锁上下文调用
    const Entry* findValid(const std::string& key) const;

    mutable std::shared_mutex               mutex_; // 读写锁
    std::unordered_map<std::string, Entry>  data_;
};
