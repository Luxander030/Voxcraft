#include "testing.h"
#include <cstring>

namespace vxtest {
std::vector<Case>& registry() { static std::vector<Case> r; return r; }
int g_failures = 0;
const char* g_current = "";
static int g_caseFailures = 0;
void fail(const char* file, int line, const std::string& msg) {
    g_failures++; g_caseFailures++;
    fprintf(stderr, "  FAIL %s (%s:%d): %s\n", g_current, file, line, msg.c_str());
}
const vx::BlockRegistry& blocks() {
    static vx::BlockRegistry r;
    static bool loaded = [] {
        std::string err;
        if (!r.loadFile(std::string(VX_SOURCE_DIR) + "/assets/data/blocks.json", &err)) { fprintf(stderr, "blocks.json: %s\n", err.c_str()); exit(2); }
        return true; }();
    (void)loaded;
    return r;
}
vx::fs::path tempDir(const std::string& name) {
    vx::fs::path p = vx::fs::temp_directory_path() / "voxcraft-tests" / name;
    std::error_code ec; vx::fs::remove_all(p, ec); vx::fs::create_directories(p, ec);
    return p;
}
vx::ItemNames testItems() {
    return {[](vx::u16 id) { return "item" + std::to_string(id); },
            [](const std::string& n) { return (vx::u16)atoi(n.c_str() + 4); }};
}
} // namespace vxtest

int main(int argc, char** argv) {
    vx::logSetLevel(vx::LogLevel::Warn);
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int run = 0, failedCases = 0;
    vx::Stopwatch total;
    for (auto& c : vxtest::registry()) {
        if (filter && !strstr(c.name, filter)) continue;
        vxtest::g_current = c.name; vxtest::g_caseFailures = 0;
        vx::Stopwatch sw;
        c.fn();
        run++;
        if (vxtest::g_caseFailures) failedCases++;
        printf("%s %-44s %8.1f ms\n", vxtest::g_caseFailures ? "FAIL" : " ok ", c.name, sw.ms());
    }
    printf("\n%d test(s), %d failed, %d check failure(s), %.0f ms\n", run, failedCases, vxtest::g_failures, total.ms());
    return failedCases ? 1 : 0;
}
