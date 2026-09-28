#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace gincy {
class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };
    Json() = default;
    static Json boolean(bool value);
    static Json number(double value);
    static Json integer(std::int64_t value);
    static Json string(std::string value);
    static Json array();
    static Json object();
    static Json parse(const std::string& text);

    Type type() const { return kind; }
    bool isNull() const { return kind == Type::Null; }
    bool isBool() const { return kind == Type::Bool; }
    bool isNumber() const { return kind == Type::Number; }
    bool isString() const { return kind == Type::String; }
    bool isArray() const { return kind == Type::Array; }
    bool isObject() const { return kind == Type::Object; }
    bool isInteger() const;

    bool asBool() const { return flag; }
    double asNumber() const { return value; }
    std::int64_t asInteger() const { return static_cast<std::int64_t>(value); }
    const std::string& asString() const { return text; }
    const std::vector<Json>& items() const { return list; }
    std::vector<Json>& items() { return list; }
    const std::map<std::string, Json>& fields() const { return map; }
    std::map<std::string, Json>& fields() { return map; }

    const Json* find(const std::string& key) const;
    Json& set(const std::string& key, Json child);
    void erase(const std::string& key);
    Json& push(Json child);
    std::string dump() const;

private:
    void write(std::string& out) const;
    Type kind = Type::Null;
    bool flag = false;
    double value = 0;
    std::string text;
    std::vector<Json> list;
    std::map<std::string, Json> map;
};

std::string jsonQuote(const std::string& value);
}
