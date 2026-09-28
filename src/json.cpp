#include "json.hpp"
#include <cctype>
#include <cmath>
#include <sstream>
#include <stdexcept>
namespace gincy {
namespace {
struct Parser {
    const std::string& text;
    std::size_t at = 0;
    void skip() { while (at < text.size() && std::isspace(static_cast<unsigned char>(text[at]))) ++at; }
    char peek() { skip(); return at < text.size() ? text[at] : 0; }
    char take() { skip(); if (at >= text.size()) throw std::runtime_error("truncated json"); return text[at++]; }
    Json parseValue();
    Json parseString() {
        if (take() != '"') throw std::runtime_error("expected string");
        std::string out;
        while (at < text.size()) {
            char ch = text[at++];
            if (ch == '"') return Json::string(std::move(out));
            if (ch == '\\') {
                if (at >= text.size()) throw std::runtime_error("truncated escape");
                char esc = text[at++];
                if (esc == '"' || esc == '\\' || esc == '/') out.push_back(esc);
                else if (esc == 'n') out.push_back('\n');
                else if (esc == 'r') out.push_back('\r');
                else if (esc == 't') out.push_back('\t');
                else throw std::runtime_error("unsupported escape");
            } else out.push_back(ch);
        }
        throw std::runtime_error("unterminated string");
    }
};
Json Parser::parseValue() {
    char ch = peek();
    if (ch == '"') return parseString();
    if (ch == '{') {
        take();
        auto object = Json::object();
        if (peek() == '}') { take(); return object; }
        while (true) {
            auto key = parseString();
            if (take() != ':') throw std::runtime_error("expected colon");
            object.set(key.asString(), parseValue());
            char next = take();
            if (next == '}') break;
            if (next != ',') throw std::runtime_error("expected comma");
        }
        return object;
    }
    if (ch == '[') {
        take();
        auto array = Json::array();
        if (peek() == ']') { take(); return array; }
        while (true) {
            array.push(parseValue());
            char next = take();
            if (next == ']') break;
            if (next != ',') throw std::runtime_error("expected comma");
        }
        return array;
    }
    if (text.compare(at, 4, "true") == 0) { at += 4; return Json::boolean(true); }
    if (text.compare(at, 5, "false") == 0) { at += 5; return Json::boolean(false); }
    if (text.compare(at, 4, "null") == 0) { at += 4; return Json(); }
    skip();
    std::size_t start = at;
    if (at < text.size() && (text[at] == '-' || text[at] == '+')) ++at;
    while (at < text.size() && (std::isdigit(static_cast<unsigned char>(text[at])) || text[at] == '.' || text[at] == 'e' || text[at] == 'E' || text[at] == '+' || text[at] == '-')) ++at;
    if (start == at) throw std::runtime_error("invalid json");
    return Json::number(std::stod(text.substr(start, at - start)));
}
}
Json Json::boolean(bool value) { Json json; json.kind = Type::Bool; json.flag = value; return json; }
Json Json::number(double value) { Json json; json.kind = Type::Number; json.value = value; return json; }
Json Json::integer(std::int64_t value) { Json json; json.kind = Type::Number; json.value = static_cast<double>(value); return json; }
Json Json::string(std::string value) { Json json; json.kind = Type::String; json.text = std::move(value); return json; }
Json Json::array() { Json json; json.kind = Type::Array; return json; }
Json Json::object() { Json json; json.kind = Type::Object; return json; }
Json Json::parse(const std::string& text) {
    Parser parser{text};
    auto value = parser.parseValue();
    parser.skip();
    if (parser.at != text.size()) throw std::runtime_error("trailing json");
    return value;
}
bool Json::isInteger() const { return kind == Type::Number && std::isfinite(value) && value == std::floor(value); }
const Json* Json::find(const std::string& key) const {
    auto it = map.find(key);
    return it == map.end() ? nullptr : &it->second;
}
Json& Json::set(const std::string& key, Json child) {
    kind = Type::Object;
    return map[key] = std::move(child);
}
void Json::erase(const std::string& key) { map.erase(key); }
Json& Json::push(Json child) {
    kind = Type::Array;
    list.push_back(std::move(child));
    return list.back();
}
std::string jsonQuote(const std::string& value) {
    std::string out = "\"";
    for (unsigned char ch : value) {
        if (ch == '"' || ch == '\\') { out.push_back('\\'); out.push_back(static_cast<char>(ch)); }
        else if (ch == '\n') out += "\\n";
        else if (ch == '\r') out += "\\r";
        else if (ch == '\t') out += "\\t";
        else out.push_back(static_cast<char>(ch));
    }
    out.push_back('"');
    return out;
}
void Json::write(std::string& out) const {
    switch (kind) {
    case Type::Null: out += "null"; break;
    case Type::Bool: out += flag ? "true" : "false"; break;
    case Type::Number: {
        std::ostringstream stream;
        if (isInteger()) stream << asInteger();
        else stream.precision(17), stream << value;
        out += stream.str();
        break;
    }
    case Type::String: out += jsonQuote(text); break;
    case Type::Array:
        out.push_back('[');
        for (std::size_t i = 0; i < list.size(); ++i) { if (i) out.push_back(','); list[i].write(out); }
        out.push_back(']');
        break;
    case Type::Object: {
        out.push_back('{');
        bool first = true;
        for (const auto& entry : map) {
            if (!first) out.push_back(',');
            first = false;
            out += jsonQuote(entry.first);
            out.push_back(':');
            entry.second.write(out);
        }
        out.push_back('}');
        break;
    }
    }
}
std::string Json::dump() const { std::string out; write(out); return out; }
}
