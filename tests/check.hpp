// Minimal assert-based test runner. No third-party test framework: the project
// builds on a clean machine with a C++20 compiler and CMake, nothing else.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace check {

struct TestCase {
    std::string suite;
    std::string name;
    std::function<void()> body;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

struct Registrar {
    Registrar(const char* suite, const char* name, std::function<void()> body) {
        registry().push_back({suite, name, std::move(body)});
    }
};

struct Failure {
    std::string message;
};

[[noreturn]] inline void fail(const std::string& file, int line, const std::string& what) {
    std::ostringstream os;
    os << file << ":" << line << ": " << what;
    throw Failure{os.str()};
}

template <typename A, typename B>
void equal(const std::string& file, int line, const A& a, const B& b, const char* ea, const char* eb) {
    if (!(a == b)) {
        std::ostringstream os;
        os << "expected " << ea << " == " << eb << ", got " << a << " vs " << b;
        fail(file, line, os.str());
    }
}

inline void near(const std::string& file, int line, double a, double b, double tol,
                 const char* ea, const char* eb) {
    if (!(std::fabs(a - b) <= tol)) {
        std::ostringstream os;
        os << "expected " << ea << " ~= " << eb << " (tol " << tol << "), got " << a << " vs " << b;
        fail(file, line, os.str());
    }
}

inline int run(int argc, char** argv) {
    std::vector<std::string> wanted(argv + 1, argv + argc);
    int passed = 0, failed = 0;
    for (const auto& t : registry()) {
        if (!wanted.empty() &&
            std::find(wanted.begin(), wanted.end(), t.suite) == wanted.end())
            continue;
        try {
            t.body();
            std::cout << "  ok   " << t.suite << " / " << t.name << "\n";
            ++passed;
        } catch (const Failure& f) {
            std::cout << "  FAIL " << t.suite << " / " << t.name << "\n       " << f.message << "\n";
            ++failed;
        } catch (const std::exception& e) {
            std::cout << "  FAIL " << t.suite << " / " << t.name
                      << "\n       unexpected exception: " << e.what() << "\n";
            ++failed;
        }
    }
    std::cout << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}

}  // namespace check

#define CHECK_CONCAT_(a, b) a##b
#define CHECK_CONCAT(a, b) CHECK_CONCAT_(a, b)

#define TEST(suite, name)                                                          \
    static void CHECK_CONCAT(check_body_, __LINE__)();                             \
    static const ::check::Registrar CHECK_CONCAT(check_reg_, __LINE__)(            \
        #suite, name, &CHECK_CONCAT(check_body_, __LINE__));                       \
    static void CHECK_CONCAT(check_body_, __LINE__)()

#define CHECK_TRUE(expr) \
    do { if (!(expr)) ::check::fail(__FILE__, __LINE__, "expected true: " #expr); } while (0)
#define CHECK_FALSE(expr) \
    do { if (expr) ::check::fail(__FILE__, __LINE__, "expected false: " #expr); } while (0)
#define CHECK_EQ(a, b) ::check::equal(__FILE__, __LINE__, (a), (b), #a, #b)
#define CHECK_NEAR(a, b, tol) ::check::near(__FILE__, __LINE__, (a), (b), (tol), #a, #b)
#define CHECK_THROWS(expr, exc)                                                     \
    do {                                                                            \
        bool thrown = false;                                                        \
        try { expr; } catch (const exc&) { thrown = true; }                          \
        if (!thrown) ::check::fail(__FILE__, __LINE__, "expected " #exc " from " #expr); \
    } while (0)
