// mini_json.h — a tiny, dependency-free parser for the one shape of JSON
// this mod actually needs to read: a flat object of string keys mapping to
// strings, numbers, booleans, or nested flat objects (for "params").
//
// This is intentionally NOT a general-purpose JSON library. It exists so
// the mod doesn't need to vendor a third-party JSON dependency just to
// parse tiny command lines like:
//   {"cmd": "effect", "id": "player.give_rupees", "params": {"amount": 50}}
//
// If you'd rather depend on nlohmann/json (single header, MIT licensed),
// swap this out — net_bridge.cpp and effects.cpp only touch the JsonValue
// interface below, so the rest of the mod doesn't need to change.

#pragma once

#include <string>
#include <unordered_map>
#include <variant>
#include <memory>
#include <stdexcept>
#include <cctype>

namespace chickencontrol {

class JsonValue {
public:
    using Object = std::unordered_map<std::string, JsonValue>;

    enum class Type { Null, String, Number, Bool, Object };

    JsonValue() : type_(Type::Null) {}
    JsonValue(std::string s) : type_(Type::String), str_(std::move(s)) {}
    JsonValue(double n) : type_(Type::Number), num_(n) {}
    JsonValue(bool b) : type_(Type::Bool), bool_(b) {}
    JsonValue(Object o) : type_(Type::Object), obj_(std::make_shared<Object>(std::move(o))) {}

    Type type() const { return type_; }

    std::string as_string(const std::string& fallback = "") const {
        return type_ == Type::String ? str_ : fallback;
    }
    double as_number(double fallback = 0.0) const {
        return type_ == Type::Number ? num_ : fallback;
    }
    int as_int(int fallback = 0) const {
        return type_ == Type::Number ? static_cast<int>(num_) : fallback;
    }
    bool as_bool(bool fallback = false) const {
        return type_ == Type::Bool ? bool_ : fallback;
    }

    // Returns Null JsonValue if this isn't an object or the key is absent.
    const JsonValue& get(const std::string& key) const {
        static JsonValue null_value;
        if (type_ != Type::Object || !obj_) return null_value;
        auto it = obj_->find(key);
        return it == obj_->end() ? null_value : it->second;
    }

    bool has(const std::string& key) const {
        return type_ == Type::Object && obj_ && obj_->count(key) > 0;
    }

private:
    Type type_;
    std::string str_;
    double num_ = 0.0;
    bool bool_ = false;
    std::shared_ptr<Object> obj_;
};

// Parses a single JSON object from `text`. Throws std::runtime_error on
// malformed input. Only supports the subset described above (object,
// string, number, bool, null, nested object) — no arrays, since the wire
// protocol never sends any.
class JsonParser {
public:
    static JsonValue parse(const std::string& text) {
        JsonParser p(text);
        p.skip_ws();
        JsonValue v = p.parse_value();
        return v;
    }

private:
    explicit JsonParser(const std::string& text) : s_(text), pos_(0) {}

    void skip_ws() {
        while (pos_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[pos_]))) pos_++;
    }

    char peek() {
        if (pos_ >= s_.size()) throw std::runtime_error("unexpected end of JSON input");
        return s_[pos_];
    }

    char advance() { return s_[pos_++]; }

    void expect(char c) {
        if (peek() != c) throw std::runtime_error(std::string("expected '") + c + "'");
        pos_++;
    }

    JsonValue parse_value() {
        skip_ws();
        char c = peek();
        if (c == '{') return parse_object();
        if (c == '"') return JsonValue(parse_string());
        if (c == 't' || c == 'f') return parse_bool();
        if (c == 'n') { expect_literal("null"); return JsonValue(); }
        return parse_number();
    }

    JsonValue parse_object() {
        expect('{');
        JsonValue::Object obj;
        skip_ws();
        if (peek() == '}') { pos_++; return JsonValue(std::move(obj)); }
        while (true) {
            skip_ws();
            std::string key = parse_string();
            skip_ws();
            expect(':');
            JsonValue value = parse_value();
            obj.emplace(std::move(key), std::move(value));
            skip_ws();
            char c = advance();
            if (c == '}') break;
            if (c != ',') throw std::runtime_error("expected ',' or '}' in object");
        }
        return JsonValue(std::move(obj));
    }

    std::string parse_string() {
        expect('"');
        std::string out;
        while (true) {
            char c = advance();
            if (c == '"') break;
            if (c == '\\') {
                char esc = advance();
                switch (esc) {
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    default: out += esc; break;
                }
            } else {
                out += c;
            }
        }
        return out;
    }

    JsonValue parse_bool() {
        if (peek() == 't') { expect_literal("true"); return JsonValue(true); }
        expect_literal("false");
        return JsonValue(false);
    }

    void expect_literal(const char* lit) {
        for (const char* p = lit; *p; ++p) {
            if (advance() != *p) throw std::runtime_error("invalid literal");
        }
    }

    JsonValue parse_number() {
        size_t start = pos_;
        if (peek() == '-') pos_++;
        while (pos_ < s_.size() && (std::isdigit(static_cast<unsigned char>(s_[pos_])) || s_[pos_] == '.' ||
                                     s_[pos_] == 'e' || s_[pos_] == 'E' || s_[pos_] == '+' || s_[pos_] == '-')) {
            pos_++;
        }
        return JsonValue(std::stod(s_.substr(start, pos_ - start)));
    }

    const std::string& s_;
    size_t pos_;
};

} // namespace chickencontrol
