// Minimal JSON reader and writer.
//
// The scenario format is JSON rather than XML because the document is a plain
// tree of numbers, strings and lists with no mixed content, no attributes and
// no namespaces. A conforming XML reader is several times the code of this file
// for a document shape that gains nothing from it. Scenarios also get committed
// next to the report, and JSON diffs line by line without closing tags doubling
// the noise.
//
// Objects keep insertion order (a vector of pairs, not a map). That keeps the
// dumped document stable across runs, which matters when a scenario is diffed,
// and sidesteps the incomplete-type question that std::map<std::string, Value>
// would raise inside Value itself.
#pragma once

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace quorum::json {

class Value;

using Array = std::vector<Value>;
using Object = std::vector<std::pair<std::string, Value>>;

// Raised for malformed input and for type or field errors on a parsed document.
class Error : public std::runtime_error {
public:
    explicit Error(const std::string& what) : std::runtime_error(what) {}
};

class Value {
public:
    using Storage = std::variant<std::monostate, bool, double, std::string, Array, Object>;

    Value() = default;
    Value(bool b) : v_(b) {}
    Value(double d) : v_(d) {}
    Value(int i) : v_(static_cast<double>(i)) {}
    Value(const char* s) : v_(std::string(s)) {}
    Value(std::string s) : v_(std::move(s)) {}
    Value(Array a) : v_(std::move(a)) {}
    Value(Object o) : v_(std::move(o)) {}

    bool is_null() const { return std::holds_alternative<std::monostate>(v_); }
    bool is_bool() const { return std::holds_alternative<bool>(v_); }
    bool is_number() const { return std::holds_alternative<double>(v_); }
    bool is_string() const { return std::holds_alternative<std::string>(v_); }
    bool is_array() const { return std::holds_alternative<Array>(v_); }
    bool is_object() const { return std::holds_alternative<Object>(v_); }

    // Typed accessors. `where` is the caller's description of the document
    // position, so a bad scenario names the field instead of the offset.
    bool as_bool(std::string_view where) const;
    double as_number(std::string_view where) const;
    const std::string& as_string(std::string_view where) const;
    const Array& as_array(std::string_view where) const;
    const Object& as_object(std::string_view where) const;

    // Object member lookup. `find` returns nullptr when absent, `at` throws.
    const Value* find(std::string_view key) const;
    const Value& at(std::string_view key, std::string_view where) const;

    double number_or(std::string_view key, double fallback, std::string_view where) const;
    std::string string_or(std::string_view key, std::string fallback, std::string_view where) const;

    Storage& storage() { return v_; }
    const Storage& storage() const { return v_; }

private:
    Storage v_;
};

Value parse(std::string_view text);
Value parse_file(const std::string& path);
std::string dump(const Value& v, int indent = 2);

}  // namespace quorum::json
