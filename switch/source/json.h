#pragma once
/* Minimal JSON parser — enough for .upsong charts and the station
   manifest.json (objects, arrays, strings, numbers, true/false/null).
   Values are held by shared_ptr so children stay cheap to copy. */
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace ujs {

struct Value;
using Val = std::shared_ptr<Value>;
using Obj = std::unordered_map<std::string, Val>;
using Arr = std::vector<Val>;

struct Value {
    enum { Null, Bool, Num, Str, O, A } type = Null;
    bool b = false;
    double num = 0;
    std::string str;
    Obj obj;
    Arr arr;
    const Val& get(const char* key) const;      // null-Val when absent
    bool isObj() const { return type == O; }
    bool isArr() const { return type == A; }
    bool isStr() const { return type == Str; }
    bool isNum() const { return type == Num; }
    int    asInt(int d = 0) const;
    double asNum(double d = 0) const;
    std::string asStr(const char* d = "") const;
};

// Parse a document; throws ParseError on malformed input.
struct ParseError : std::exception {
    std::string msg;
    ParseError(std::string m) : msg(std::move(m)) {}
    const char* what() const noexcept override { return msg.c_str(); }
};

Val parse(const std::string& text);

} // namespace ujs
