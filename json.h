// json.h
// A small, self-contained JSON *parser* (the engine already had a JSON
// *writer* in scheduler.cpp). Deliberately hand-written instead of pulling
// in a library -- the project brief is explicit about avoiding "a huge
// framework just for this," and our JSON needs are simple: objects,
// arrays, strings, numbers, booleans, all flat/shallow.
#pragma once
#include <string>
#include <vector>
#include <map>
#include <cctype>
#include <stdexcept>

struct JsonValue {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool b = false;
    double num = 0;
    std::string str;
    std::vector<JsonValue> arr;
    std::map<std::string, JsonValue> obj;

    const JsonValue* get(const std::string& key) const {
        if (type != Object) return nullptr;
        auto it = obj.find(key);
        return it == obj.end() ? nullptr : &it->second;
    }
    double getNumber(const std::string& key, double def = 0) const {
        const JsonValue* v = get(key);
        return (v && v->type == Number) ? v->num : def;
    }
    std::string getString(const std::string& key, const std::string& def = "") const {
        const JsonValue* v = get(key);
        return (v && v->type == String) ? v->str : def;
    }
    bool getBool(const std::string& key, bool def = false) const {
        const JsonValue* v = get(key);
        return (v && v->type == Bool) ? v->b : def;
    }
};

class JsonParser {
    const std::string& s;
    size_t i = 0;

public:
    explicit JsonParser(const std::string& src) : s(src) {}
    JsonValue parse() { skipWs(); return parseValue(); }

private:
    void skipWs() { while (i < s.size() && std::isspace((unsigned char)s[i])) i++; }
    char peek() const { return i < s.size() ? s[i] : '\0'; }
    char next() { return s[i++]; }

    JsonValue parseValue() {
        skipWs();
        char c = peek();
        if (c == '{') return parseObject();
        if (c == '[') return parseArray();
        if (c == '"') return parseString();
        if (c == 't' || c == 'f') return parseBool();
        if (c == 'n') { i += 4; return JsonValue(); } // "null"
        return parseNumber();
    }

    JsonValue parseObject() {
        JsonValue v; v.type = JsonValue::Object;
        next(); skipWs();
        if (peek() == '}') { next(); return v; }
        while (true) {
            skipWs();
            JsonValue key = parseString();
            skipWs();
            if (peek() != ':') throw std::runtime_error("expected ':' in JSON object");
            next(); // consume ':'
            JsonValue val = parseValue();
            v.obj[key.str] = val;
            skipWs();
            if (peek() == ',') { next(); continue; }
            break;
        }
        skipWs();
        if (peek() != '}') throw std::runtime_error("expected '}' in JSON object");
        next();
        return v;
    }

    JsonValue parseArray() {
        JsonValue v; v.type = JsonValue::Array;
        next(); skipWs();
        if (peek() == ']') { next(); return v; }
        while (true) {
            v.arr.push_back(parseValue());
            skipWs();
            if (peek() == ',') { next(); skipWs(); continue; }
            break;
        }
        skipWs();
        if (peek() != ']') throw std::runtime_error("expected ']' in JSON array");
        next();
        return v;
    }

    JsonValue parseString() {
        JsonValue v; v.type = JsonValue::String;
        if (peek() != '"') throw std::runtime_error("expected string in JSON");
        next(); // opening quote
        std::string out;
        while (peek() != '"') {
            if (i >= s.size()) throw std::runtime_error("unterminated JSON string");
            char c = next();
            if (c == '\\') {
                char e = next();
                switch (e) {
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case 'r': out += '\r'; break;
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    default: out += e;
                }
            } else out += c;
        }
        next(); // closing quote
        v.str = out;
        return v;
    }

    JsonValue parseBool() {
        JsonValue v; v.type = JsonValue::Bool;
        if (s.compare(i, 4, "true") == 0) { v.b = true; i += 4; }
        else { v.b = false; i += 5; } // "false"
        return v;
    }

    JsonValue parseNumber() {
        JsonValue v; v.type = JsonValue::Number;
        size_t start = i;
        if (peek() == '-') i++;
        while (i < s.size() && (std::isdigit((unsigned char)s[i]) || s[i] == '.' ||
               s[i] == 'e' || s[i] == 'E' || s[i] == '+' || s[i] == '-')) i++;
        if (i == start) throw std::runtime_error("invalid JSON number");
        v.num = std::stod(s.substr(start, i - start));
        return v;
    }
};
