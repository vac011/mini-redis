#include "Store.h"
#include <algorithm>
#include <mutex>

// ---- MemoryStore 实现 ----

const Entry* MemoryStore::findValid(const std::string& key) const {
    auto it = data_.find(key);
    if (it == data_.end()) return nullptr;
    if (it->second.isExpired()) return nullptr;
    return &it->second;
}

std::optional<std::string> MemoryStore::get(const std::string& key) const {
    std::shared_lock lock(mutex_); // 读锁
    auto* entry = findValid(key);
    if (!entry) return std::nullopt;
    return entry->value;
}

void MemoryStore::set(const std::string& key, std::string value,
                      std::chrono::seconds ttl) {
    std::unique_lock lock(mutex_); // 写锁
    Entry entry;
    entry.value = std::move(value);
    if (ttl.count() > 0) {
        entry.expireAt = std::chrono::steady_clock::now() + ttl;
    }
    data_[key] = std::move(entry);
}

int MemoryStore::del(const std::vector<std::string>& keys) {
    std::unique_lock lock(mutex_);
    int count = 0;
    for (auto& k : keys) {
        count += data_.erase(k);
    }
    return count;
}

bool MemoryStore::exists(const std::string& key) const {
    std::shared_lock lock(mutex_);
    return findValid(key) != nullptr;
}

std::vector<std::string> MemoryStore::keys(const std::string& pattern) const {
    std::shared_lock lock(mutex_);
    std::vector<std::string> result;
    // 简化：仅支持 "*" 匹配所有
    if (pattern == "*") {
        for (auto& [k, v] : data_) {
            if (!v.isExpired()) result.push_back(k);
        }
    }
    return result;
}

std::optional<long long> MemoryStore::incr(const std::string& key, long long delta) {
    std::unique_lock lock(mutex_);
    auto it = data_.find(key);
    if (it == data_.end()) {
        // 不存在，初始化为 0 + delta
        Entry entry;
        entry.value = std::to_string(delta);
        data_[key] = std::move(entry);
        return delta;
    }
    if (it->second.isExpired()) {
        // 已过期，重新初始化
        it->second.value = std::to_string(delta);
        it->second.expireAt = std::chrono::steady_clock::time_point::max();
        return delta;
    }
    // 尝试解析为整数
    try {
        long long val = std::stoll(it->second.value);
        val += delta;
        it->second.value = std::to_string(val);
        return val;
    } catch (...) {
        return std::nullopt; // 不是整数
    }
}

int MemoryStore::evictExpired() {
    std::unique_lock lock(mutex_);
    int count = 0;
    for (auto it = data_.begin(); it != data_.end(); ) {
        if (it->second.isExpired()) {
            it = data_.erase(it);
            ++count;
        } else {
            ++it;
        }
    }
    return count;
}
