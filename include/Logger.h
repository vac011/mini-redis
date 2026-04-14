#pragma once
#include <iostream>
#include <sstream>
#include <mutex>
#include <chrono>
#include <iomanip>

// CRTP 编译时多态：不同日志级别在编译期确定
template<typename Derived>
class LoggerBase {
public:
    template<typename... Args>
    void log(Args&&... args) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ostringstream oss;
        oss << getTimestamp() << " [" << static_cast<Derived*>(this)->getLevel() << "] ";
        (oss << ... << std::forward<Args>(args));
        std::cout << oss.str() << std::endl;
    }

private:
    std::string getTimestamp() {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::ostringstream oss;
        oss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
        return oss.str();
    }

    static std::mutex mutex_;
};

template<typename Derived>
std::mutex LoggerBase<Derived>::mutex_;

// 派生类：编译时确定日志级别
class InfoLogger : public LoggerBase<InfoLogger> {
public:
    const char* getLevel() { return "INFO"; }
};

class ErrorLogger : public LoggerBase<ErrorLogger> {
public:
    const char* getLevel() { return "ERROR"; }
};

class DebugLogger : public LoggerBase<DebugLogger> {
public:
    const char* getLevel() { return "DEBUG"; }
};

// 全局单例
inline InfoLogger& LOG_INFO() {
    static InfoLogger logger;
    return logger;
}

inline ErrorLogger& LOG_ERROR() {
    static ErrorLogger logger;
    return logger;
}

inline DebugLogger& LOG_DEBUG() {
    static DebugLogger logger;
    return logger;
}
