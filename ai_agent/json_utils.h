/*
 * json_utils.h - Lightweight JSON Parser
 * 
 * Part of the AIAgent reusable module.
 * Single-header library: define AI_AGENT_IMPLEMENTATION in ONE .cpp file
 * before including this header to get the implementation.
 *
 * Features:
 *   - Parse JSON strings/numbers/booleans/arrays/objects
 *   - Extract nested values by key path (e.g., "choices[0].message.content")
 *   - Build JSON objects for API requests
 *   - No external dependencies
 *
 * Usage:
 *   #include "json_utils.h"      // declarations only
 *   // In one .cpp file:
 *   #define AI_AGENT_IMPLEMENTATION
 *   #include "json_utils.h"      // includes implementation
 */

#ifndef JSON_UTILS_H
#define JSON_UTILS_H

#include <string>
#include <vector>
#include <map>

namespace aiagent {

// JSON value types
enum class JsonType { Null, Bool, Number, String, Array, Object };

// Forward declaration
class JsonValue;
using JsonObject = std::map<std::string, JsonValue>;
using JsonArray = std::vector<JsonValue>;

class JsonValue {
public:
    JsonType type;
    bool boolVal;
    double numVal;
    std::string strVal;
    JsonArray arrVal;
    JsonObject objVal;

    JsonValue() : type(JsonType::Null), boolVal(false), numVal(0) {}
    
    // Constructors
    static JsonValue makeString(const std::string& s);
    static JsonValue makeNumber(double n);
    static JsonValue makeBool(bool b);
    static JsonValue makeArray();
    static JsonValue makeObject();
    static JsonValue makeNull();

    // Accessors
    const JsonValue& operator[](const std::string& key) const;
    const JsonValue& operator[](int index) const;
    std::string asString() const;
    double asNumber() const;
    bool asBool() const;
    bool isNull() const;
    
    // Extract nested value by path like "choices[0].message.content"
    const JsonValue& getPath(const std::string& path) const;
};

// Parse a JSON string into a JsonValue
JsonValue jsonParse(const std::string& json);

// Build a JSON string from a JsonValue
std::string jsonStringify(const JsonValue& val);

// Convenience: build a chat message object
JsonValue jsonChatMessage(const std::string& role, const std::string& content);

// Escape a string for JSON output
std::string jsonEscape(const std::string& s);

} // namespace aiagent

// ============================================================
// Implementation
// ============================================================
#ifdef AI_AGENT_IMPLEMENTATION
#ifndef JSON_UTILS_IMPL_GUARD
#define JSON_UTILS_IMPL_GUARD

#include <sstream>
#include <cstdlib>
#include <cstring>

namespace aiagent {

static const JsonValue NULL_JSON_VALUE;

JsonValue JsonValue::makeString(const std::string& s) {
    JsonValue v; v.type = JsonType::String; v.strVal = s; return v;
}
JsonValue JsonValue::makeNumber(double n) {
    JsonValue v; v.type = JsonType::Number; v.numVal = n; return v;
}
JsonValue JsonValue::makeBool(bool b) {
    JsonValue v; v.type = JsonType::Bool; v.boolVal = b; return v;
}
JsonValue JsonValue::makeArray() {
    JsonValue v; v.type = JsonType::Array; return v;
}
JsonValue JsonValue::makeObject() {
    JsonValue v; v.type = JsonType::Object; return v;
}
JsonValue JsonValue::makeNull() {
    return JsonValue();
}

const JsonValue& JsonValue::operator[](const std::string& key) const {
    if (type != JsonType::Object) return NULL_JSON_VALUE;
    auto it = objVal.find(key);
    if (it == objVal.end()) return NULL_JSON_VALUE;
    return it->second;
}

const JsonValue& JsonValue::operator[](int index) const {
    if (type != JsonType::Array || index < 0 || index >= (int)arrVal.size())
        return NULL_JSON_VALUE;
    return arrVal[index];
}

std::string JsonValue::asString() const {
    if (type == JsonType::String) return strVal;
    if (type == JsonType::Number) {
        std::ostringstream oss; oss << numVal; return oss.str();
    }
    if (type == JsonType::Bool) return boolVal ? "true" : "false";
    return "";
}

double JsonValue::asNumber() const {
    if (type == JsonType::Number) return numVal;
    if (type == JsonType::String) return atof(strVal.c_str());
    return 0;
}

bool JsonValue::asBool() const {
    if (type == JsonType::Bool) return boolVal;
    return false;
}

bool JsonValue::isNull() const {
    return type == JsonType::Null;
}

const JsonValue& JsonValue::getPath(const std::string& path) const {
    const JsonValue* cur = this;
    size_t i = 0;
    while (i < path.size()) {
        if (path[i] == '[') {
            size_t end = path.find(']', i);
            if (end == std::string::npos) return NULL_JSON_VALUE;
            int idx = atoi(path.substr(i + 1, end - i - 1).c_str());
            cur = &((*cur)[idx]);
            i = end + 1;
            if (i < path.size() && path[i] == '.') i++;
        } else {
            size_t dot = path.find_first_of(".[", i);
            std::string key = (dot == std::string::npos) ? path.substr(i) : path.substr(i, dot - i);
            cur = &((*cur)[key]);
            i = (dot == std::string::npos) ? path.size() : dot;
            if (i < path.size() && path[i] == '.') i++;
        }
        if (cur->isNull()) return NULL_JSON_VALUE;
    }
    return *cur;
}

// JSON Parser
struct JsonParser {
    const char* s;
    size_t pos;
    size_t len;
    
    void skipWhitespace() {
        while (pos < len && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n' || s[pos] == '\r'))
            pos++;
    }
    
    char peek() { skipWhitespace(); return pos < len ? s[pos] : '\0'; }
    char next() { skipWhitespace(); return pos < len ? s[pos++] : '\0'; }
    
    std::string parseString() {
        if (next() != '"') return "";
        std::string result;
        while (pos < len && s[pos] != '"') {
            if (s[pos] == '\\' && pos + 1 < len) {
                pos++;
                switch (s[pos]) {
                    case '"': result += '"'; break;
                    case '\\': result += '\\'; break;
                    case '/': result += '/'; break;
                    case 'b': result += '\b'; break;
                    case 'f': result += '\f'; break;
                    case 'n': result += '\n'; break;
                    case 'r': result += '\r'; break;
                    case 't': result += '\t'; break;
                    case 'u': {
                        if (pos + 4 < len) {
                            // Basic unicode escape - just pass through for now
                            unsigned int cp = 0;
                            for (int i = 0; i < 4; i++) {
                                pos++;
                                char c = s[pos];
                                cp <<= 4;
                                if (c >= '0' && c <= '9') cp |= (c - '0');
                                else if (c >= 'a' && c <= 'f') cp |= (c - 'a' + 10);
                                else if (c >= 'A' && c <= 'F') cp |= (c - 'A' + 10);
                            }
                            // Simple UTF-8 encoding
                            if (cp < 0x80) {
                                result += (char)cp;
                            } else if (cp < 0x800) {
                                result += (char)(0xC0 | (cp >> 6));
                                result += (char)(0x80 | (cp & 0x3F));
                            } else {
                                result += (char)(0xE0 | (cp >> 12));
                                result += (char)(0x80 | ((cp >> 6) & 0x3F));
                                result += (char)(0x80 | (cp & 0x3F));
                            }
                        }
                        break;
                    }
                    default: result += s[pos]; break;
                }
            } else {
                result += s[pos];
            }
            pos++;
        }
        if (pos < len) pos++; // skip closing quote
        return result;
    }
    
    JsonValue parseValue() {
        char c = peek();
        if (c == '"') {
            return JsonValue::makeString(parseString());
        } else if (c == '{') {
            return parseObject();
        } else if (c == '[') {
            return parseArray();
        } else if (c == 't' || c == 'f') {
            return parseBool();
        } else if (c == 'n') {
            return parseNull();
        } else {
            return parseNumber();
        }
    }
    
    JsonValue parseObject() {
        JsonValue obj = JsonValue::makeObject();
        next(); // skip '{'
        if (peek() == '}') { next(); return obj; }
        while (true) {
            std::string key = parseString();
            next(); // skip ':'
            obj.objVal[key] = parseValue();
            if (peek() == ',') { next(); continue; }
            break;
        }
        next(); // skip '}'
        return obj;
    }
    
    JsonValue parseArray() {
        JsonValue arr = JsonValue::makeArray();
        next(); // skip '['
        if (peek() == ']') { next(); return arr; }
        while (true) {
            arr.arrVal.push_back(parseValue());
            if (peek() == ',') { next(); continue; }
            break;
        }
        next(); // skip ']'
        return arr;
    }
    
    JsonValue parseNumber() {
        skipWhitespace();
        size_t start = pos;
        if (pos < len && (s[pos] == '-' || s[pos] == '+')) pos++;
        while (pos < len && s[pos] >= '0' && s[pos] <= '9') pos++;
        if (pos < len && s[pos] == '.') {
            pos++;
            while (pos < len && s[pos] >= '0' && s[pos] <= '9') pos++;
        }
        if (pos < len && (s[pos] == 'e' || s[pos] == 'E')) {
            pos++;
            if (pos < len && (s[pos] == '+' || s[pos] == '-')) pos++;
            while (pos < len && s[pos] >= '0' && s[pos] <= '9') pos++;
        }
        return JsonValue::makeNumber(atof(std::string(s + start, pos - start).c_str()));
    }
    
    JsonValue parseBool() {
        skipWhitespace();
        if (pos + 4 <= len && strncmp(s + pos, "true", 4) == 0) {
            pos += 4; return JsonValue::makeBool(true);
        }
        if (pos + 5 <= len && strncmp(s + pos, "false", 5) == 0) {
            pos += 5; return JsonValue::makeBool(false);
        }
        return JsonValue::makeBool(false);
    }
    
    JsonValue parseNull() {
        skipWhitespace();
        if (pos + 4 <= len && strncmp(s + pos, "null", 4) == 0) pos += 4;
        return JsonValue::makeNull();
    }
};

JsonValue jsonParse(const std::string& json) {
    JsonParser p;
    p.s = json.c_str();
    p.pos = 0;
    p.len = json.size();
    return p.parseValue();
}

std::string jsonEscape(const std::string& s) {
    std::string result;
    result.reserve(s.size() + 16);
    for (char c : s) {
        switch (c) {
            case '"':  result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if ((unsigned char)c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)c);
                    result += buf;
                } else {
                    result += c;
                }
        }
    }
    return result;
}

std::string jsonStringify(const JsonValue& val) {
    switch (val.type) {
        case JsonType::Null: return "null";
        case JsonType::Bool: return val.boolVal ? "true" : "false";
        case JsonType::Number: {
            std::ostringstream oss;
            if (val.numVal == (int)val.numVal) oss << (int)val.numVal;
            else oss << val.numVal;
            return oss.str();
        }
        case JsonType::String:
            return "\"" + jsonEscape(val.strVal) + "\"";
        case JsonType::Array: {
            std::string s = "[";
            for (size_t i = 0; i < val.arrVal.size(); i++) {
                if (i > 0) s += ",";
                s += jsonStringify(val.arrVal[i]);
            }
            return s + "]";
        }
        case JsonType::Object: {
            std::string s = "{";
            bool first = true;
            for (auto& kv : val.objVal) {
                if (!first) s += ",";
                s += "\"" + jsonEscape(kv.first) + "\":" + jsonStringify(kv.second);
                first = false;
            }
            return s + "}";
        }
    }
    return "null";
}

JsonValue jsonChatMessage(const std::string& role, const std::string& content) {
    JsonValue msg = JsonValue::makeObject();
    msg.objVal["role"] = JsonValue::makeString(role);
    msg.objVal["content"] = JsonValue::makeString(content);
    return msg;
}

} // namespace aiagent

#endif // JSON_UTILS_IMPL_GUARD
#endif // AI_AGENT_IMPLEMENTATION
#endif // JSON_UTILS_H
