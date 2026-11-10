#include <cstdint>
#include <cstdio>

namespace th {
bool png_size_allowed(uint64_t size);
}

int main() {
    constexpr uint64_t kMax = 32ull * 1024 * 1024;
    if (th::png_size_allowed(0)) {
        std::fprintf(stderr, "empty PNG input was accepted\n");
        return 1;
    }
    if (!th::png_size_allowed(1) || !th::png_size_allowed(kMax)) {
        std::fprintf(stderr, "valid PNG sizes were rejected\n");
        return 2;
    }
    if (th::png_size_allowed(kMax + 1) || th::png_size_allowed(UINT64_MAX)) {
        std::fprintf(stderr, "oversized PNG input was accepted\n");
        return 3;
    }
    std::puts("PASS PNG size boundaries");
    return 0;
}
