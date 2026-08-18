#include "quorum/json.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace quorum::json {
namespace {

std::string quote(std::string_view s) { return "'" + std::string(s) + "'"; }

class Parser {
public:
    explicit Parser(std::string_view text) : t_(text) {}

    Value parse_document() {
        skip_ws();
        Value v = parse_value();
        skip_ws();
        if (i_ != t_.size()) error("trailing characters after the document");
        return v;
    }

private:
    std::string_view t_;
    std::size_t i_ = 0;

    [[noreturn]] void error(const std::string& what) const {
        std::size_t line = 1, col = 1;
        for (std::size_t k = 0; k < i_ && k < t_.size(); ++k) {
            if (t_[k] == '\n') {
                ++line;
                col = 1;
            } else {
                ++col;
            }
        }
        throw Error("JSON " + std::to_string(line) + ":" + std::to_string(col) + ": " + what);
    }

    void skip_ws() {
        while (i_ < t_.size()) {
            char c = t_[i_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++i_;
                continue;
            }
            // Line comments are not JSON, but a scenario is a document a human
            // maintains, and an unexplained magic number is worse than a
            // one-character extension to the reader.
            if (c == '/' && i_ + 1 < t_.size() && t_[i_ + 1] == '/') {
                while (i_ < t_.size() && t_[i_] != '\n') ++i_;
                continue;
            }
            break;
        }
    }

    char peek() const { return i_ < t_.size() ? t_[i_] : '\0'; }

    bool literal(std::string_view word) {
        if (t_.substr(i_, word.size()) == word) {
            i_ += word.size();
            return true;
        }
        return false;
    }

    Value parse_value() {
        skip_ws();
        if (i_ >= t_.size()) error("unexpected end of document");
        switch (peek()) {
            case '{':
                return parse_object();
            case '[':
                return parse_array();
            case '"':
                return Value(parse_string());
            case 't':
                if (literal("true")) return Value(true);
                error("bad literal, expected true");
            case 'f':
                if (literal("false")) return Value(false);
                error("bad literal, expected false");
            case 'n':
                if (literal("null")) return Value();
                error("bad literal, expected null");
            default:
                return parse_number();
        }
    }

    Value parse_object() {
        ++i_;  // opening brace
        Object out;
        skip_ws();
        if (peek() == '}') {
            ++i_;
            return Value(std::move(out));
        }
        for (;;) {
            skip_ws();
            if (peek() != '"') error("expected a member name");
            std::string key = parse_string();
            skip_ws();
            if (peek() != ':') error("expected a colon after the member name");
            ++i_;
            out.emplace_back(std::move(key), parse_value());
            skip_ws();
            if (peek() == ',') {
                ++i_;
                continue;
            }
            if (peek() == '}') {
                ++i_;
                return Value(std::move(out));
            }
            error("expected a comma or a closing brace");
        }
    }

    Value parse_array() {
        ++i_;  // opening bracket
        Array out;
        skip_ws();
        if (peek() == ']') {
            ++i_;
            return Value(std::move(out));
        }
        for (;;) {
            out.push_back(parse_value());
            skip_ws();
            if (peek() == ',') {
                ++i_;
                continue;
            }
            if (peek() == ']') {
                ++i_;
                return Value(std::move(out));
            }
            error("expected a comma or a closing bracket");
        }
    }

    std::string parse_string() {
        ++i_;  // opening quote
        std::string out;
        while (i_ < t_.size()) {
            char c = t_[i_++];
            if (c == '"') return out;
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (i_ >= t_.size()) break;
            char e = t_[i_++];
            switch (e) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': append_utf8(out); break;
                default: error("unknown escape sequence");
            }
        }
        error("unterminated string");
    }

    // Decodes one four-digit escape and appends its UTF-8 encoding. Surrogate
    // pairs are out of scope: scenario text stays in the basic multilingual
    // plane, and a wrong guess at pairing would corrupt the identifier silently.
    void append_utf8(std::string& out) {
        if (i_ + 4 > t_.size()) error("truncated four-digit escape");
        unsigned code = 0;
        for (int k = 0; k < 4; ++k) {
            char h = t_[i_++];
            unsigned d = 0;
            if (h >= '0' && h <= '9')
                d = static_cast<unsigned>(h - '0');
            else if (h >= 'a' && h <= 'f')
                d = static_cast<unsigned>(h - 'a' + 10);
            else if (h >= 'A' && h <= 'F')
                d = static_cast<unsigned>(h - 'A' + 10);
            else
                error("bad hex digit in a four-digit escape");
            code = code * 16 + d;
        }
        if (code < 0x80) {
            out.push_back(static_cast<char>(code));
        } else if (code < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (code >> 6)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xE0 | (code >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        }
    }

    Value parse_number() {
        std::size_t start = i_;
        if (peek() == '-' || peek() == '+') ++i_;
        bool digits = false;
        while (i_ < t_.size() && std::isdigit(static_cast<unsigned char>(t_[i_]))) {
            ++i_;
            digits = true;
        }
        if (i_ < t_.size() && t_[i_] == '.') {
            ++i_;
            while (i_ < t_.size() && std::isdigit(static_cast<unsigned char>(t_[i_]))) {
                ++i_;
                digits = true;
            }
        }
        if (!digits) error("expected a value");
        if (i_ < t_.size() && (t_[i_] == 'e' || t_[i_] == 'E')) {
            ++i_;
            if (i_ < t_.size() && (t_[i_] == '-' || t_[i_] == '+')) ++i_;
            while (i_ < t_.size() && std::isdigit(static_cast<unsigned char>(t_[i_]))) ++i_;
        }
        return Value(std::stod(std::string(t_.substr(start, i_ - start))));
    }
};

void dump_into(const Value& v, std::string& out, int indent, int level);

void dump_string(const std::string& s, std::string& out) {
    out += '"';
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            case '\r': out += "\\r"; break;
            default: out += c;
        }
    }
    out += '"';
}

void dump_number(double d, std::string& out) {
    char buf[32];
    if (d == std::floor(d) && std::fabs(d) < 1e15)
        std::snprintf(buf, sizeof buf, "%.0f", d);
    else
        std::snprintf(buf, sizeof buf, "%.6g", d);
    out += buf;
}

void dump_into(const Value& v, std::string& out, int indent, int level) {
    const std::string pad(static_cast<std::size_t>(indent * level), ' ');
    const std::string pad_in(static_cast<std::size_t>(indent * (level + 1)), ' ');
    if (v.is_null()) {
        out += "null";
    } else if (v.is_bool()) {
        out += v.as_bool("dump") ? "true" : "false";
    } else if (v.is_number()) {
        dump_number(v.as_number("dump"), out);
    } else if (v.is_string()) {
        dump_string(v.as_string("dump"), out);
    } else if (v.is_array()) {
        const Array& a = v.as_array("dump");
        if (a.empty()) {
            out += "[]";
            return;
        }
        out += "[\n";
        for (std::size_t k = 0; k < a.size(); ++k) {
            out += pad_in;
            dump_into(a[k], out, indent, level + 1);
            out += (k + 1 < a.size()) ? ",\n" : "\n";
        }
        out += pad + "]";
    } else {
        const Object& o = v.as_object("dump");
        if (o.empty()) {
            out += "{}";
            return;
        }
        out += "{\n";
        for (std::size_t k = 0; k < o.size(); ++k) {
            out += pad_in;
            dump_string(o[k].first, out);
            out += ": ";
            dump_into(o[k].second, out, indent, level + 1);
            out += (k + 1 < o.size()) ? ",\n" : "\n";
        }
        out += pad + "}";
    }
}

}  // namespace

bool Value::as_bool(std::string_view where) const {
    if (!is_bool()) throw Error(std::string(where) + ": expected a boolean");
    return std::get<bool>(v_);
}

double Value::as_number(std::string_view where) const {
    if (!is_number()) throw Error(std::string(where) + ": expected a number");
    return std::get<double>(v_);
}

const std::string& Value::as_string(std::string_view where) const {
    if (!is_string()) throw Error(std::string(where) + ": expected a string");
    return std::get<std::string>(v_);
}

const Array& Value::as_array(std::string_view where) const {
    if (!is_array()) throw Error(std::string(where) + ": expected an array");
    return std::get<Array>(v_);
}

const Object& Value::as_object(std::string_view where) const {
    if (!is_object()) throw Error(std::string(where) + ": expected an object");
    return std::get<Object>(v_);
}

const Value* Value::find(std::string_view key) const {
    if (!is_object()) return nullptr;
    for (const auto& entry : std::get<Object>(v_))
        if (entry.first == key) return &entry.second;
    return nullptr;
}

const Value& Value::at(std::string_view key, std::string_view where) const {
    const Value* v = find(key);
    if (!v) throw Error(std::string(where) + ": missing required member " + quote(key));
    return *v;
}

double Value::number_or(std::string_view key, double fallback, std::string_view where) const {
    const Value* v = find(key);
    if (!v || v->is_null()) return fallback;
    return v->as_number(std::string(where) + "." + std::string(key));
}

std::string Value::string_or(std::string_view key, std::string fallback, std::string_view where) const {
    const Value* v = find(key);
    if (!v || v->is_null()) return fallback;
    return v->as_string(std::string(where) + "." + std::string(key));
}

Value parse(std::string_view text) { return Parser(text).parse_document(); }

Value parse_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw Error("cannot open " + path);
    std::ostringstream buf;
    buf << in.rdbuf();
    return parse(buf.str());
}

std::string dump(const Value& v, int indent) {
    std::string out;
    dump_into(v, out, indent, 0);
    out += "\n";
    return out;
}

}  // namespace quorum::json
