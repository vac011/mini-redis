#include "Command.h"
#include "Store.h"
#include "RespCodec.h"
#include <algorithm>
#include <stdexcept>

// ---- 工厂方法 -----------------------------------------------------------
// 运行时多态的入口：根据命令名返回不同的具体子类

std::unique_ptr<Command> Command::create(const std::vector<std::string>& args) {
    if (args.empty()) return nullptr;

    std::string cmd = args[0];
    std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::toupper);

    if (cmd == "PING") {
        if (args.size() >= 2)
            return std::make_unique<PingCommand>(args[1]);
        return std::make_unique<PingCommand>();
    }
    if (cmd == "GET") {
        if (args.size() < 2) return nullptr;
        return std::make_unique<GetCommand>(args[1]);
    }
    if (cmd == "SET") {
        if (args.size() < 3) return nullptr;
        int ttl = -1;
        for (size_t i = 3; i + 1 < args.size(); ++i) {
            std::string opt = args[i];
            std::transform(opt.begin(), opt.end(), opt.begin(), ::toupper);
            if (opt == "EX") {
                try { ttl = std::stoi(args[i+1]); } catch (...) {}
            }
        }
        return std::make_unique<SetCommand>(args[1], args[2], ttl);
    }
    if (cmd == "DEL") {
        if (args.size() < 2) return nullptr;
        std::vector<std::string> keys(args.begin() + 1, args.end());
        return std::make_unique<DelCommand>(std::move(keys));
    }
    if (cmd == "EXISTS") {
        if (args.size() < 2) return nullptr;
        return std::make_unique<ExistsCommand>(args[1]);
    }
    if (cmd == "KEYS") {
        if (args.size() < 2) return nullptr;
        return std::make_unique<KeysCommand>(args[1]);
    }
    if (cmd == "INCR") {
        if (args.size() < 2) return nullptr;
        return std::make_unique<IncrCommand>(args[1]);
    }
    if (cmd == "DECR") {
        if (args.size() < 2) return nullptr;
        return std::make_unique<DecrCommand>(args[1]);
    }
    return nullptr;
}

// ---- 各命令实现（运行时多态的虚函数体）----------------------------------

// PING
std::string PingCommand::execute(Store& /*store*/) {
    return RespCodec::encode(RespValue::simpleString(msg_));
}

// GET
std::string GetCommand::execute(Store& store) {
    auto val = store.get(key_);
    if (!val)
        return RespCodec::encode(RespValue::null());
    return RespCodec::encode(RespValue::bulkString(*val));
}

// SET
std::string SetCommand::execute(Store& store) {
    std::chrono::seconds ttl(ttl_ > 0 ? ttl_ : -1);
    store.set(key_, value_, ttl);
    return RespCodec::encode(RespValue::simpleString("OK"));
}

// DEL
std::string DelCommand::execute(Store& store) {
    int n = store.del(keys_);
    return RespCodec::encode(RespValue::makeInt(n));
}

// EXISTS
std::string ExistsCommand::execute(Store& store) {
    int n = store.exists(key_) ? 1 : 0;
    return RespCodec::encode(RespValue::makeInt(n));
}

// KEYS
std::string KeysCommand::execute(Store& store) {
    auto ks = store.keys(pattern_);
    std::vector<RespValue> arr;
    arr.reserve(ks.size());
    for (auto& k : ks)
        arr.push_back(RespValue::bulkString(k));
    return RespCodec::encode(RespValue::makeArray(std::move(arr)));
}

// INCR
std::string IncrCommand::execute(Store& store) {
    auto v = store.incr(key_, 1);
    if (!v)
        return RespCodec::encode(
            RespValue::error("ERR value is not an integer or out of range"));
    return RespCodec::encode(RespValue::makeInt(*v));
}

// DECR
std::string DecrCommand::execute(Store& store) {
    auto v = store.incr(key_, -1);
    if (!v)
        return RespCodec::encode(
            RespValue::error("ERR value is not an integer or out of range"));
    return RespCodec::encode(RespValue::makeInt(*v));
}
