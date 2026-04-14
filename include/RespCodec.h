#pragma once
#include <string>
#include <vector>

// RESP (REdis Serialization Protocol) 值类型
enum class RespType {
    SimpleString,  // +OK\r\n
    Error,         // -ERR ...\r\n
    Integer,       // :42\r\n
    BulkString,    // $6\r\nfoobar\r\n
    Array,         // *3\r\n...
    Null           // $-1\r\n
};

// RESP 值节点（递归结构）
// 注意：字段名避免与工厂函数重名
struct RespValue {
    RespType type{RespType::Null};
    std::string str;              // SimpleString / Error / BulkString
    long long   intVal{0};        // Integer
    std::vector<RespValue> elements; // Array

    // 工厂辅助（静态方法与字段同名会冲突，统一加 make 前缀）
    static RespValue simpleString(std::string s) {
        RespValue r; r.type = RespType::SimpleString; r.str = std::move(s); return r;
    }
    static RespValue error(std::string s) {
        RespValue r; r.type = RespType::Error; r.str = std::move(s); return r;
    }
    static RespValue makeInt(long long v) {
        RespValue r; r.type = RespType::Integer; r.intVal = v; return r;
    }
    static RespValue bulkString(std::string s) {
        RespValue r; r.type = RespType::BulkString; r.str = std::move(s); return r;
    }
    static RespValue null() {
        RespValue r; r.type = RespType::Null; return r;
    }
    static RespValue makeArray(std::vector<RespValue> arr) {
        RespValue r; r.type = RespType::Array; r.elements = std::move(arr); return r;
    }
};

// 解析结果
struct ParseResult {
    bool        ok{false};     // 是否成功
    RespValue   value;
    size_t      consumed{0};   // 消耗的字节数（绝对偏移）
    std::string error;         // 错误信息
};

// RESP 协议编解码器
class RespCodec {
public:
    // 编码：RespValue → 字节串
    static std::string encode(const RespValue& val);

    // 解码：字节缓冲区 → RespValue
    // 支持流式解析（缓冲区可能不完整时返回 ok=false）
    static ParseResult decode(const char* data, size_t len);

    // 便捷：从参数列表构造 RESP 数组字节串
    static std::string encodeInline(const std::vector<std::string>& args);

private:
    static ParseResult parseValue(const char* data, size_t len, size_t offset);
    static ParseResult parseSimpleString(const char* data, size_t len, size_t offset);
    static ParseResult parseError(const char* data, size_t len, size_t offset);
    static ParseResult parseInteger(const char* data, size_t len, size_t offset);
    static ParseResult parseBulkString(const char* data, size_t len, size_t offset);
    static ParseResult parseArray(const char* data, size_t len, size_t offset);

    // 查找 \r\n，返回位置，找不到返回 std::string::npos
    static size_t findCRLF(const char* data, size_t len, size_t offset);
};
