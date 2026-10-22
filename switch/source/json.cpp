#include "json.h"
#include <cctype>
#include <cstdlib>

namespace ujs {

static const Val& nullv() {
    static const Val v = std::make_shared<Value>();
    return v;
}

const Val& Value::get(const char* key) const {
    if (type != O) return nullv();
    auto it = obj.find(key);
    return it == obj.end() ? nullv() : it->second;
}

int Value::asInt(int d) const {
    return type == Num ? (int)num : d;
}
double Value::asNum(double d) const {
    return type == Num ? num : d;
}
std::string Value::asStr(const char* d) const {
    return type == Str ? str : std::string(d);
}

struct P {
    const char* p;
    const char* end;

    void ws() { while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p; }
    [[noreturn]] void err(const char* m) { throw ParseError(m); }

    Val value() {
        ws();
        if (p >= end) err("unexpected end of input");
        switch (*p) {
        case '{': return object();
        case '[': return array();
        case '"': { auto v = std::make_shared<Value>(); v->type = Value::Str; v->str = string(); return v; }
        case 't': lit("true"); { auto v = std::make_shared<Value>(); v->type = Value::Bool; v->b = true; return v; }
        case 'f': lit("false"); { auto v = std::make_shared<Value>(); v->type = Value::Bool; v->b = false; return v; }
        case 'n': lit("null"); return nullv();
        default: return number();
        }
    }

    void lit(const char* s) {
        while (*s) { if (p >= end || *p != *s) err("bad literal"); ++p; ++s; }
    }

    std::string string() {
        ++p;                       // opening quote
        std::string out;
        while (p < end) {
            char c = *p++;
            if (c == '"') return out;
            if (c == '\\') {
                if (p >= end) err("bad escape");
                char e = *p++;
                switch (e) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    if (end - p < 4) err("bad \\u escape");
                    unsigned cp = std::strtoul(std::string(p, 4).c_str(), nullptr, 16);
                    p += 4;
                    // surrogate pair support
                    if (cp >= 0xD800 && cp <= 0xDBFF && end - p >= 6 && p[0] == '\\' && p[1] == 'u') {
                        unsigned lo = std::strtoul(std::string(p + 2, 4).c_str(), nullptr, 16);
                        if (lo >= 0xDC00 && lo <= 0xDFFF) { p += 6; cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); }
                    }
                    if (cp < 0x80) out += (char)cp;
                    else if (cp < 0x800) { out += (char)(0xC0 | cp >> 6); out += (char)(0x80 | (cp & 63)); }
                    else if (cp < 0x10000) { out += (char)(0xE0 | cp >> 12); out += (char)(0x80 | (cp >> 6 & 63)); out += (char)(0x80 | (cp & 63)); }
                    else { out += (char)(0xF0 | cp >> 18); out += (char)(0x80 | (cp >> 12 & 63)); out += (char)(0x80 | (cp >> 6 & 63)); out += (char)(0x80 | (cp & 63)); }
                    break;
                }
                default: out += e; break;   // ", \\, /, etc.
                }
            } else {
                out += c;
            }
        }
        err("unterminated string");
    }

    Val number() {
        const char* s = p;
        while (p < end && (*p == '-' || *p == '+' || *p == '.' || *p == 'e' || *p == 'E' || std::isdigit((unsigned char)*p))) ++p;
        if (p == s) err("expected value");
        auto v = std::make_shared<Value>();
        v->type = Value::Num;
        v->num = std::strtod(std::string(s, p).c_str(), nullptr);
        return v;
    }

    Val object() {
        ++p;
        auto v = std::make_shared<Value>();
        v->type = Value::O;
        ws();
        if (p < end && *p == '}') { ++p; return v; }
        while (true) {
            ws();
            if (p >= end || *p != '"') err("expected string key");
            std::string key = string();
            ws();
            if (p >= end || *p != ':') err("expected ':'");
            ++p;
            v->obj[std::move(key)] = value();
            ws();
            if (p >= end) err("unterminated object");
            if (*p == '}') { ++p; return v; }
            if (*p != ',') err("expected ','");
            ++p;
        }
    }

    Val array() {
        ++p;
        auto v = std::make_shared<Value>();
        v->type = Value::A;
        ws();
        if (p < end && *p == ']') { ++p; return v; }
        while (true) {
            v->arr.push_back(value());
            ws();
            if (p >= end) err("unterminated array");
            if (*p == ']') { ++p; return v; }
            if (*p != ',') err("expected ','");
            ++p;
        }
    }
};

Val parse(const std::string& text) {
    P parser{text.c_str(), text.c_str() + text.size()};
    Val v = parser.value();
    parser.ws();
    if (parser.p != parser.end) throw ParseError("trailing data");
    return v;
}

} // namespace ujs
