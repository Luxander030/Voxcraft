// Logging, binary serialization, CRC32, filesystem helpers, timing.
#pragma once
#include "core/common.h"
#include <chrono>
#include <filesystem>
#include <optional>
#include <string_view>

namespace vx {

// ---------------------------------------------------------------- logging
enum class LogLevel { Debug, Info, Warn, Error };
void logSetLevel(LogLevel l);
void logSetFile(const std::filesystem::path& p);
void logWrite(LogLevel l, const char* fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;
#define VX_DEBUG(...) ::vx::logWrite(::vx::LogLevel::Debug, __VA_ARGS__)
#define VX_INFO(...) ::vx::logWrite(::vx::LogLevel::Info, __VA_ARGS__)
#define VX_WARN(...) ::vx::logWrite(::vx::LogLevel::Warn, __VA_ARGS__)
#define VX_ERROR(...) ::vx::logWrite(::vx::LogLevel::Error, __VA_ARGS__)

// ---------------------------------------------------------------- timing
inline double nowSeconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}
struct Stopwatch { double t0 = nowSeconds(); double ms() const { return (nowSeconds() - t0) * 1000.0; } };

// ---------------------------------------------------------------- CRC32 (IEEE, table driven)
u32 crc32(const void* data, size_t n, u32 crc = 0);

// ---------------------------------------------------------------- binary I/O (little endian, varints)
class ByteWriter {
public:
    std::vector<u8> buf;
    void u8_(u8 v) { buf.push_back(v); }
    void u16_(u16 v) { u8_((u8)v); u8_((u8)(v >> 8)); }
    void u32_(u32 v) { for (int i = 0; i < 4; i++) u8_((u8)(v >> (i * 8))); }
    void u64_(u64 v) { for (int i = 0; i < 8; i++) u8_((u8)(v >> (i * 8))); }
    void i32_(i32 v) { u32_((u32)v); }
    void f32_(float f) { u32 v; std::memcpy(&v, &f, 4); u32_(v); }
    void f64_(double f) { u64 v; std::memcpy(&v, &f, 8); u64_(v); }
    void var(u64 v) { while (v >= 0x80) { u8_((u8)(v | 0x80)); v >>= 7; } u8_((u8)v); }
    void svar(i64 v) { var(((u64)v << 1) ^ (u64)(v >> 63)); }   // zigzag
    void bytes(const void* p, size_t n) { const u8* b = (const u8*)p; buf.insert(buf.end(), b, b + n); }
    void str(std::string_view s) { var(s.size()); bytes(s.data(), s.size()); }
    size_t size() const { return buf.size(); }
    void patchU32(size_t at, u32 v) { for (int i = 0; i < 4; i++) buf[at + i] = (u8)(v >> (i * 8)); }
};

class ByteReader {
public:
    const u8* p; size_t n; size_t pos = 0; bool bad = false;
    ByteReader(const void* data, size_t size) : p((const u8*)data), n(size) {}
    explicit ByteReader(const std::vector<u8>& v) : p(v.data()), n(v.size()) {}
    bool ok() const { return !bad; }
    size_t remaining() const { return bad ? 0 : n - pos; }
    u8 u8_() { if (pos + 1 > n) { bad = true; return 0; } return p[pos++]; }
    u16 u16_() { u16 a = u8_(); return (u16)(a | (u8_() << 8)); }
    u32 u32_() { u32 v = 0; for (int i = 0; i < 4; i++) v |= (u32)u8_() << (i * 8); return v; }
    u64 u64_() { u64 v = 0; for (int i = 0; i < 8; i++) v |= (u64)u8_() << (i * 8); return v; }
    i32 i32_() { return (i32)u32_(); }
    float f32_() { u32 v = u32_(); float f; std::memcpy(&f, &v, 4); return f; }
    double f64_() { u64 v = u64_(); double f; std::memcpy(&f, &v, 8); return f; }
    u64 var() { u64 v = 0; for (int s = 0; s < 64; s += 7) { u8 b = u8_(); v |= (u64)(b & 0x7f) << s; if (!(b & 0x80)) return v; } bad = true; return 0; }
    i64 svar() { u64 v = var(); return (i64)(v >> 1) ^ -(i64)(v & 1); }
    bool bytes(void* out, size_t k) { if (pos + k > n) { bad = true; return false; } std::memcpy(out, p + pos, k); pos += k; return true; }
    const u8* skip(size_t k) { if (pos + k > n) { bad = true; return nullptr; } const u8* r = p + pos; pos += k; return r; }
    std::string str() { u64 k = var(); if (k > remaining()) { bad = true; return {}; } std::string s((const char*)p + pos, (size_t)k); pos += (size_t)k; return s; }
};

// ---------------------------------------------------------------- compression (LZ4 block, size-prefixed)
std::vector<u8> compressLZ4(const u8* data, size_t n);
bool decompressLZ4(const u8* data, size_t n, size_t rawSize, std::vector<u8>& out);

// ---------------------------------------------------------------- filesystem
namespace fs = std::filesystem;
// UTF-8 string -> path, correct on Windows (where fs::path(std::string) uses the ANSI code page).
inline fs::path pathFromUtf8(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::optional<std::vector<u8>> readFile(const fs::path& p);
std::optional<std::string> readTextFile(const fs::path& p);
// Writes to "<p>.tmp", flushes to stable storage, then atomically renames over p.
bool writeFileAtomic(const fs::path& p, const void* data, size_t n);
inline bool writeFileAtomic(const fs::path& p, const std::vector<u8>& v) { return writeFileAtomic(p, v.data(), v.size()); }
// Writes + fsyncs a file without renaming (used by the save journal).
bool writeFileDurable(const fs::path& p, const void* data, size_t n);
bool renameReplace(const fs::path& from, const fs::path& to);
bool syncDirectory(const fs::path& dir);
u64 directorySize(const fs::path& dir);

// Per-user writable data directory (Application Support on macOS, %APPDATA% on Windows,
// XDG_DATA_HOME elsewhere). Never hardcoded.
fs::path userDataDir(const char* appName);
// Directory containing the running executable.
fs::path executableDir();
// Resident memory of this process in bytes (0 if unavailable).
u64 processMemoryBytes();
// Installed physical memory in bytes (0 if unavailable).
u64 physicalMemoryBytes();

} // namespace vx
