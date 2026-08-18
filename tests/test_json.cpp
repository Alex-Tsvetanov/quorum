#include "check.hpp"
#include "quorum/json.hpp"

using namespace quorum;

TEST(json, "nested objects and arrays keep their structure and order") {
    const auto v = json::parse(R"({"a": 1, "b": [2, 3, {"c": "x"}], "d": {"e": true}})");
    CHECK_TRUE(v.is_object());
    CHECK_NEAR(v.at("a", "root").as_number("a"), 1.0, 1e-12);
    const auto& b = v.at("b", "root").as_array("b");
    CHECK_EQ(b.size(), std::size_t{3});
    CHECK_NEAR(b[1].as_number("b1"), 3.0, 1e-12);
    CHECK_EQ(b[2].at("c", "b2").as_string("c"), std::string("x"));
    CHECK_TRUE(v.at("d", "root").at("e", "d").as_bool("e"));
    // Insertion order is part of the contract: a dumped scenario has to diff
    // against the one it was read from.
    const auto& members = v.as_object("root");
    CHECK_EQ(members.size(), std::size_t{3});
    CHECK_EQ(members[0].first, std::string("a"));
    CHECK_EQ(members[1].first, std::string("b"));
    CHECK_EQ(members[2].first, std::string("d"));
}

TEST(json, "numbers cover sign, fraction and exponent") {
    const auto v = json::parse(R"([-3, 0.25, 2.5e2, -1.5e-2, 0])");
    const auto& a = v.as_array("root");
    CHECK_NEAR(a[0].as_number("0"), -3.0, 1e-12);
    CHECK_NEAR(a[1].as_number("1"), 0.25, 1e-12);
    CHECK_NEAR(a[2].as_number("2"), 250.0, 1e-12);
    CHECK_NEAR(a[3].as_number("3"), -0.015, 1e-12);
    CHECK_NEAR(a[4].as_number("4"), 0.0, 1e-12);
}

TEST(json, "escapes decode, including the four-digit form") {
    const auto v = json::parse(R"({"s": "a\"b\\c\nd"})");
    CHECK_EQ(v.at("s", "root").as_string("s"), std::string("a\"b\\c\nd"));

    // 0041 is the letter A, one UTF-8 byte. 00E9 is e with an acute accent, two.
    const auto u = json::parse("{\"s\": \"\\u0041\\u00e9\"}");
    const std::string decoded = u.at("s", "root").as_string("s");
    CHECK_EQ(decoded.size(), std::size_t{3});
    CHECK_EQ(decoded[0], 'A');
    CHECK_EQ(static_cast<int>(static_cast<unsigned char>(decoded[1])), 0xC3);
    CHECK_EQ(static_cast<int>(static_cast<unsigned char>(decoded[2])), 0xA9);
}

TEST(json, "line comments are skipped wherever whitespace is allowed") {
    const auto v = json::parse("// leading\n{ // after the brace\n \"a\": 1 // trailing\n }");
    CHECK_NEAR(v.at("a", "root").as_number("a"), 1.0, 1e-12);
}

TEST(json, "malformed input is rejected rather than half accepted") {
    CHECK_THROWS(json::parse("{\"a\": 1"), json::Error);
    CHECK_THROWS(json::parse("{\"a\" 1}"), json::Error);
    CHECK_THROWS(json::parse("[1, 2"), json::Error);
    CHECK_THROWS(json::parse("\"unterminated"), json::Error);
    CHECK_THROWS(json::parse("{} trailing"), json::Error);
    CHECK_THROWS(json::parse("nul"), json::Error);
}

TEST(json, "type and member errors name the position the caller gave") {
    const auto v = json::parse(R"({"a": 1})");
    bool named = false;
    try {
        v.at("missing", "scenario.projects[0]");
    } catch (const json::Error& e) {
        named = std::string(e.what()).find("scenario.projects[0]") != std::string::npos;
    }
    CHECK_TRUE(named);
    CHECK_THROWS(v.at("a", "root").as_string("root.a"), json::Error);
}

TEST(json, "dump then parse returns the same document") {
    const std::string source =
        R"({"name": "s", "n": 12, "f": 1.5, "list": [1, 2, 3], "flag": false, "empty": [],)"
        R"( "obj": {"k": "v"}, "nothing": null})";
    const auto first = json::parse(source);
    const auto second = json::parse(json::dump(first));
    CHECK_EQ(json::dump(first), json::dump(second));
    CHECK_EQ(second.at("name", "root").as_string("name"), std::string("s"));
    CHECK_NEAR(second.at("f", "root").as_number("f"), 1.5, 1e-12);
    CHECK_TRUE(second.at("nothing", "root").is_null());
    CHECK_EQ(second.at("empty", "root").as_array("empty").size(), std::size_t{0});
}

TEST(json, "optional readers fall back without throwing") {
    const auto v = json::parse(R"({"a": 4, "b": null})");
    CHECK_NEAR(v.number_or("a", -1.0, "root"), 4.0, 1e-12);
    CHECK_NEAR(v.number_or("b", -1.0, "root"), -1.0, 1e-12);
    CHECK_NEAR(v.number_or("c", -1.0, "root"), -1.0, 1e-12);
    CHECK_EQ(v.string_or("c", "fallback", "root"), std::string("fallback"));
}
