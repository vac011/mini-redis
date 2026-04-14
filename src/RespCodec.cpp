#include "RespCodec.h"
#include <sstream>
#include <stdexcept>
#include <cstring>

// ---- 编码 ---------------------------------------------------------------

std::string RespCodec::encode(const RespValue& val) {
    std::ostringstream oss;
    switch (val.type) {
        case RespType::SimpleString:
            oss << "+" << val.str << "\r\n";
            break;
        case RespType::Error:
            oss << "-" << val.str << "\r\n";
            break;
        case RespType::Integer:
            oss << ":" << val.intVal << "\r\n";
            break;
        case RespType::BulkString:
            oss << "$" << val.str.size() << "\r\n" << val.str << "\r\n";
            break;
        case RespType::Null:
            oss << "$-1\r\n";
            break;
        case RespType::Array:
            oss << "*" << val.elements.size() << "\r\n";
            for (auto& elem : val.elements) {
                oss << encode(elem);
            }
            break;
    }
    return oss.str();
}

std::string RespCodec::encodeInline(const std::vector<std::string>& args) {
    std::ostringstream oss;
    oss << "*" << args.size() << "\r\n";
    for (auto& a : args) {
        oss << "$" << a.size() << "\r\n" << a << "\r\n";
    }
    return oss.str();
}

// ---- 工具 ---------------------------------------------------------------

size_t RespCodec::findCRLF(const char* data, size_t len, size_t offset) {
    for (size_t i = offset; i + 1 < len; ++i) {
        if (data[i] == '\r' && data[i+1] == '\n')
            return i;
    }
    return std::string::npos;
}

// ---- 解码各类型 ----------------------------------------------------------

ParseResult RespCodec::parseSimpleString(const char* data, size_t len, size_t offset) {
    size_t crlf = findCRLF(data, len, offset);
    if (crlf == std::string::npos)
        return {false, {}, 0, "Incomplete simple string"};
    ParseResult r;
    r.ok       = true;
    r.value    = RespValue::simpleString(std::string(data + offset, crlf - offset));
    r.consumed = crlf + 2;
    return r;
}

ParseResult RespCodec::parseError(const char* data, size_t len, size_t offset) {
    size_t crlf = findCRLF(data, len, offset);
    if (crlf == std::string::npos)
        return {false, {}, 0, "Incomplete error"};
    ParseResult r;
    r.ok       = true;
    r.value    = RespValue::error(std::string(data + offset, crlf - offset));
    r.consumed = crlf + 2;
    return r;
}

ParseResult RespCodec::parseInteger(const char* data, size_t len, size_t offset) {
    size_t crlf = findCRLF(data, len, offset);
    if (crlf == std::string::npos)
        return {false, {}, 0, "Incomplete integer"};
    try {
        long long v = std::stoll(std::string(data + offset, crlf - offset));
        ParseResult r;
        r.ok       = true;
        r.value    = RespValue::makeInt(v);
        r.consumed = crlf + 2;
        return r;
    } catch (...) {
        return {false, {}, 0, "Invalid integer"};
    }
}

ParseResult RespCodec::parseBulkString(const char* data, size_t len, size_t offset) {
    size_t crlf = findCRLF(data, len, offset);
    if (crlf == std::string::npos)
        return {false, {}, 0, "Incomplete bulk string length"};

    long long strLen = std::stoll(std::string(data + offset, crlf - offset));
    if (strLen == -1) {
        ParseResult r;
        r.ok       = true;
        r.value    = RespValue::null();
        r.consumed = crlf + 2;
        return r;
    }
    if (strLen < 0)
        return {false, {}, 0, "Invalid bulk string length"};

    size_t dataStart = crlf + 2;
    if (dataStart + (size_t)strLen + 2 > len)
        return {false, {}, 0, "Incomplete bulk string data"};

    ParseResult r;
    r.ok       = true;
    r.value    = RespValue::bulkString(std::string(data + dataStart, strLen));
    r.consumed = dataStart + strLen + 2;
    return r;
}

ParseResult RespCodec::parseArray(const char* data, size_t len, size_t offset) {
    size_t crlf = findCRLF(data, len, offset);
    if (crlf == std::string::npos)
        return {false, {}, 0, "Incomplete array length"};

    long long count = std::stoll(std::string(data + offset, crlf - offset));
    if (count == -1) {
        ParseResult r;
        r.ok       = true;
        r.value    = RespValue::null();
        r.consumed = crlf + 2;
        return r;
    }
    if (count < 0)
        return {false, {}, 0, "Invalid array length"};

    size_t cursor = crlf + 2;
    std::vector<RespValue> elems;
    elems.reserve(count);

    for (long long i = 0; i < count; ++i) {
        auto sub = parseValue(data, len, cursor);
        if (!sub.ok) return sub;
        cursor = sub.consumed;  // consumed 是绝对偏移
        elems.push_back(std::move(sub.value));
    }

    ParseResult r;
    r.ok       = true;
    r.value    = RespValue::makeArray(std::move(elems));
    r.consumed = cursor;
    return r;
}

ParseResult RespCodec::parseValue(const char* data, size_t len, size_t offset) {
    if (offset >= len)
        return {false, {}, 0, "Empty input"};

    char   prefix = data[offset];
    size_t next   = offset + 1;

    switch (prefix) {
        case '+': return parseSimpleString(data, len, next);
        case '-': return parseError       (data, len, next);
        case ':': return parseInteger     (data, len, next);
        case '$': return parseBulkString  (data, len, next);
        case '*': return parseArray       (data, len, next);
        default:
            return {false, {}, 0,
                    std::string("Unknown RESP prefix '") + prefix + "'"};
    }
}

ParseResult RespCodec::decode(const char* data, size_t len) {
    return parseValue(data, len, 0);
}
