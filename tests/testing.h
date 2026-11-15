// Minimal test framework: TEST(name) { CHECK(...); } - registered statically, run by test_main.cpp.
#pragma once
#include "core/common.h"
#include "core/util.h"
#include "world/block.h"
#include "storage/storage.h"
#include <type_traits>
#include <cstdio>
#include <functional>
#include <string>

namespace vxtest {
struct Case { const char* name; void (*fn)(); };
std::vector<Case>& registry();
struct Reg { Reg(const char* n, void (*f)()) { registry().push_back({n, f}); } };
extern int g_failures;
template <class T> std::string show(const T& v) {
    if constexpr (std::is_convertible_v<T, std::string>) return "\"" + std::string(v) + "\"";
    else if constexpr (std::is_enum_v<T>) return std::to_string((long long)v);
    else return std::to_string(v);
}
extern const char* g_current;
void fail(const char* file, int line, const std::string& msg);

// shared fixtures
const vx::BlockRegistry& blocks();
vx::fs::path tempDir(const std::string& name);   // fresh empty directory
vx::ItemNames testItems();
} // namespace vxtest

#define TEST(name) static void test_##name(); static vxtest::Reg reg_##name(#name, test_##name); static void test_##name()
#define CHECK(cond) do { if (!(cond)) vxtest::fail(__FILE__, __LINE__, "CHECK(" #cond ")"); } while (0)
#define CHECK_EQ(a, b) do { auto _va = (a); auto _vb = (b); if (!(_va == _vb)) vxtest::fail(__FILE__, __LINE__, std::string("CHECK_EQ(" #a ", " #b ") got ") + vxtest::show(_va) + " vs " + vxtest::show(_vb)); } while (0)
#define REQUIRE(cond) do { if (!(cond)) { vxtest::fail(__FILE__, __LINE__, "REQUIRE(" #cond ")"); return; } } while (0)
