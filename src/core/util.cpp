#include "core/util.h"
#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <lz4/lz4.h>

#if defined(_WIN32)
#include <windows.h>
#include <shlobj.h>
#include <psapi.h>
#include <io.h>
#else
#include <fcntl.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <mach/mach.h>
#endif
#endif

namespace vx {

// ---------------------------------------------------------------- logging
static LogLevel g_level = LogLevel::Info;
static std::mutex g_logMutex;
static FILE* g_logFile = nullptr;

void logSetLevel(LogLevel l) { g_level = l; }
void logSetFile(const fs::path& p) {
    std::lock_guard<std::mutex> lk(g_logMutex);
    if (g_logFile) fclose(g_logFile);
#if defined(_WIN32)
    g_logFile = _wfopen(p.wstring().c_str(), L"w");
#else
    g_logFile = fopen(p.string().c_str(), "w");
#endif
}
void logWrite(LogLevel l, const char* fmt, ...) {
    if (l < g_level) return;
    static const char* names[] = {"debug", "info", "warn", "error"};
    char msg[2048];
    va_list ap; va_start(ap, fmt); vsnprintf(msg, sizeof msg, fmt, ap); va_end(ap);
    std::lock_guard<std::mutex> lk(g_logMutex);
    FILE* out = l >= LogLevel::Warn ? stderr : stdout;
    fprintf(out, "[%s] %s\n", names[(int)l], msg);
    if (g_logFile) { fprintf(g_logFile, "[%s] %s\n", names[(int)l], msg); fflush(g_logFile); }
}

// ---------------------------------------------------------------- crc32
u32 crc32(const void* data, size_t n, u32 crc) {
    static u32 table[256];
    static bool init = [] {
        for (u32 i = 0; i < 256; i++) { u32 c = i; for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1; table[i] = c; }
        return true; }();
    (void)init;
    const u8* p = (const u8*)data; crc = ~crc;
    for (size_t i = 0; i < n; i++) crc = table[(crc ^ p[i]) & 0xff] ^ (crc >> 8);
    return ~crc;
}

// ---------------------------------------------------------------- lz4
std::vector<u8> compressLZ4(const u8* data, size_t n) {
    std::vector<u8> out(LZ4_compressBound((int)n));
    int k = LZ4_compress_default((const char*)data, (char*)out.data(), (int)n, (int)out.size());
    out.resize(k > 0 ? (size_t)k : 0);
    return out;
}
bool decompressLZ4(const u8* data, size_t n, size_t rawSize, std::vector<u8>& out) {
    out.resize(rawSize);
    int k = LZ4_decompress_safe((const char*)data, (char*)out.data(), (int)n, (int)rawSize);
    return k == (int)rawSize;
}

// ---------------------------------------------------------------- files
std::optional<std::vector<u8>> readFile(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return std::nullopt;
    f.seekg(0, std::ios::end); auto sz = f.tellg(); f.seekg(0);
    if (sz < 0) return std::nullopt;
    std::vector<u8> v((size_t)sz);
    if (sz > 0 && !f.read((char*)v.data(), sz)) return std::nullopt;
    return v;
}
std::optional<std::string> readTextFile(const fs::path& p) {
    auto v = readFile(p); if (!v) return std::nullopt;
    return std::string(v->begin(), v->end());
}

bool writeFileDurable(const fs::path& p, const void* data, size_t n) {
#if defined(_WIN32)
    HANDLE h = CreateFileW(p.wstring().c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    const u8* b = (const u8*)data; size_t left = n; bool ok = true;
    while (left > 0) { DWORD w = 0; DWORD chunk = (DWORD)std::min<size_t>(left, 1u << 30);
        if (!WriteFile(h, b, chunk, &w, nullptr) || w == 0) { ok = false; break; } b += w; left -= w; }
    if (ok) ok = FlushFileBuffers(h) != 0;
    CloseHandle(h);
    return ok;
#else
    int fd = ::open(p.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return false;
    const u8* b = (const u8*)data; size_t left = n; bool ok = true;
    while (left > 0) { ssize_t w = ::write(fd, b, left); if (w <= 0) { ok = false; break; } b += w; left -= (size_t)w; }
#if defined(__APPLE__)
    // fsync on macOS only reaches the drive cache; F_FULLFSYNC forces it to media.
    if (ok && fcntl(fd, F_FULLFSYNC) != 0) ok = ::fsync(fd) == 0;
#else
    if (ok) ok = ::fsync(fd) == 0;
#endif
    ::close(fd);
    return ok;
#endif
}

bool renameReplace(const fs::path& from, const fs::path& to) {
#if defined(_WIN32)
    return MoveFileExW(from.wstring().c_str(), to.wstring().c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    return ::rename(from.c_str(), to.c_str()) == 0;
#endif
}

bool syncDirectory(const fs::path& dir) {
#if defined(_WIN32)
    (void)dir; return true;   // NTFS metadata is journaled; MOVEFILE_WRITE_THROUGH covers the rename.
#else
    int fd = ::open(dir.c_str(), O_RDONLY);
    if (fd < 0) return false;
    ::fsync(fd); ::close(fd);
    return true;
#endif
}

bool writeFileAtomic(const fs::path& p, const void* data, size_t n) {
    fs::path tmp = p; tmp += ".tmp";
    if (!writeFileDurable(tmp, data, n)) return false;
    if (!renameReplace(tmp, p)) return false;
    syncDirectory(p.parent_path());
    return true;
}

u64 directorySize(const fs::path& dir) {
    std::error_code ec; u64 total = 0;
    for (auto& e : fs::recursive_directory_iterator(dir, ec)) if (e.is_regular_file(ec)) total += e.file_size(ec);
    return total;
}

fs::path userDataDir(const char* appName) {
#if defined(_WIN32)
    PWSTR w = nullptr; fs::path base;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &w))) { base = w; CoTaskMemFree(w); }
    else base = fs::temp_directory_path();
    return base / appName;
#elif defined(__APPLE__)
    const char* home = getenv("HOME");
    return fs::path(home ? home : "/tmp") / "Library" / "Application Support" / appName;
#else
    const char* xdg = getenv("XDG_DATA_HOME"); const char* home = getenv("HOME");
    fs::path base = xdg ? fs::path(xdg) : fs::path(home ? home : "/tmp") / ".local" / "share";
    return base / appName;
#endif
}

fs::path executableDir() {
#if defined(_WIN32)
    wchar_t buf[MAX_PATH * 2]; DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)(sizeof buf / sizeof buf[0]));
    return n ? fs::path(std::wstring(buf, n)).parent_path() : fs::current_path();
#elif defined(__APPLE__)
    char buf[4096]; uint32_t size = sizeof buf;
    if (_NSGetExecutablePath(buf, &size) == 0) { std::error_code ec; auto p = fs::canonical(buf, ec); return (ec ? fs::path(buf) : p).parent_path(); }
    return fs::current_path();
#else
    std::error_code ec; auto p = fs::read_symlink("/proc/self/exe", ec); return ec ? fs::current_path() : p.parent_path();
#endif
}

u64 processMemoryBytes() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc;
    return GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof pmc) ? (u64)pmc.WorkingSetSize : 0;
#elif defined(__APPLE__)
    mach_task_basic_info info; mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    return task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &count) == KERN_SUCCESS ? (u64)info.resident_size : 0;
#else
    long pages = 0; FILE* f = fopen("/proc/self/statm", "r"); if (f) { long t; if (fscanf(f, "%ld %ld", &t, &pages) != 2) pages = 0; fclose(f); }
    return (u64)pages * (u64)sysconf(_SC_PAGESIZE);
#endif
}

} // namespace vx
