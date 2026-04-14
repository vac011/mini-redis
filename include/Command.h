#pragma once
#include <string>
#include <vector>
#include <memory>

class Store; // 前向声明

// 运行时多态：Command 基类
// 所有命令通过虚函数 execute() 实现多态行为
class Command {
public:
    virtual ~Command() = default;

    // 纯虚函数：子类必须实现
    virtual std::string execute(Store& store) = 0;

    // 获取命令名称
    virtual std::string name() const = 0;

    // 工厂方法：根据 RESP 数组创建命令对象
    static std::unique_ptr<Command> create(const std::vector<std::string>& args);
};

// GET key
class GetCommand : public Command {
public:
    explicit GetCommand(std::string key) : key_(std::move(key)) {}
    std::string execute(Store& store) override;
    std::string name() const override { return "GET"; }

private:
    std::string key_;
};

// SET key value [EX seconds]
class SetCommand : public Command {
public:
    SetCommand(std::string key, std::string value, int ttl = -1)
        : key_(std::move(key)), value_(std::move(value)), ttl_(ttl) {}
    std::string execute(Store& store) override;
    std::string name() const override { return "SET"; }

private:
    std::string key_;
    std::string value_;
    int ttl_; // -1 表示永不过期
};

// DEL key [key ...]
class DelCommand : public Command {
public:
    explicit DelCommand(std::vector<std::string> keys) : keys_(std::move(keys)) {}
    std::string execute(Store& store) override;
    std::string name() const override { return "DEL"; }

private:
    std::vector<std::string> keys_;
};

// EXISTS key
class ExistsCommand : public Command {
public:
    explicit ExistsCommand(std::string key) : key_(std::move(key)) {}
    std::string execute(Store& store) override;
    std::string name() const override { return "EXISTS"; }

private:
    std::string key_;
};

// KEYS pattern (简化版：仅支持 *)
class KeysCommand : public Command {
public:
    explicit KeysCommand(std::string pattern) : pattern_(std::move(pattern)) {}
    std::string execute(Store& store) override;
    std::string name() const override { return "KEYS"; }

private:
    std::string pattern_;
};

// PING [message]
class PingCommand : public Command {
public:
    explicit PingCommand(std::string msg = "PONG") : msg_(std::move(msg)) {}
    std::string execute(Store& store) override;
    std::string name() const override { return "PING"; }

private:
    std::string msg_;
};

// INCR key
class IncrCommand : public Command {
public:
    explicit IncrCommand(std::string key) : key_(std::move(key)) {}
    std::string execute(Store& store) override;
    std::string name() const override { return "INCR"; }

private:
    std::string key_;
};

// DECR key
class DecrCommand : public Command {
public:
    explicit DecrCommand(std::string key) : key_(std::move(key)) {}
    std::string execute(Store& store) override;
    std::string name() const override { return "DECR"; }

private:
    std::string key_;
};
