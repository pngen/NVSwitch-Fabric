#pragma once
// Lightweight deterministic test framework (no external dependencies).
#include <cstdio>
#include <string>
#include <sstream>
#include <vector>

struct TestFailure { std::string msg; };

#define CHECK(cond) do { if (!(cond)) { throw TestFailure{ std::string(__FILE__) + ":" + std::to_string(__LINE__) + " CHECK(" #cond ")" }; } } while (0)
#define CHECK_EQ(a,b) do { auto _a = (a); auto _b = (b); if (!(_a == _b)) { std::ostringstream _os; _os << __FILE__ << ":" << __LINE__ << " CHECK_EQ(" #a ", " #b ") : " << _a << " != " << _b; throw TestFailure{ _os.str() }; } } while (0)
#define CHECK_NE(a,b) do { auto _a = (a); auto _b = (b); if (_a == _b) { std::ostringstream _os; _os << __FILE__ << ":" << __LINE__ << " CHECK_NE(" #a ", " #b ") : " << _a << " == " << _b; throw TestFailure{ _os.str() }; } } while (0)

struct TestCase { const char* name; void (*fn)(); };
inline std::vector<TestCase>& testRegistry() { static std::vector<TestCase> t; return t; }
struct TestRegistrar { TestRegistrar(const char* n, void (*f)()) { testRegistry().push_back({ n, f }); } };
#define TEST_CASE(name) \
  static void name(); \
  static TestRegistrar reg_##name(#name, &name); \
  static void name()

inline int runAllTests() {
    int fail = 0;
    for (auto& t : testRegistry()) {
        try { t.fn(); std::printf("PASS %s\n", t.name); }
        catch (TestFailure& e) { std::printf("FAIL %s: %s\n", t.name, e.msg.c_str()); ++fail; }
        catch (...) { std::printf("FAIL %s: unexpected exception\n", t.name); ++fail; }
    }
    return fail;
}
#define NVF_MAIN int main() { return runAllTests(); }
