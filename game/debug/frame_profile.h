#pragma once
#include <chrono>
#include <cstdint>
#include <vector>
// Main-thread frame profiler: named scopes accumulate total and worst time.
// Two clock reads per scope; read with `profile` in the console or the
// "profile" key of the debug state, which also resets the counters.
namespace altaria::profile {
struct Slot {
    const char *name;
    double total_ms = 0, max_ms = 0;
    uint64_t calls = 0;
};
inline std::vector<Slot *> &slots() {
    static std::vector<Slot *> all;
    return all;
}
inline Slot &slot(const char *name) {
    auto *created = new Slot{name};
    slots().push_back(created);
    return *created;
}
struct Scope {
    Slot &target;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    explicit Scope(Slot &s) : target(s) {}
    ~Scope() {
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        target.total_ms += ms;
        target.max_ms = ms > target.max_ms ? ms : target.max_ms;
        ++target.calls;
    }
};
inline void reset() {
    for (auto *s : slots())
        s->total_ms = s->max_ms = 0, s->calls = 0;
}
} // namespace altaria::profile
#define ALTARIA_PROFILE_JOIN2(a, b) a##b
#define ALTARIA_PROFILE_JOIN(a, b) ALTARIA_PROFILE_JOIN2(a, b)
#define ALTARIA_PROFILE(name)                                                                             \
    static altaria::profile::Slot &ALTARIA_PROFILE_JOIN(profile_slot_, __LINE__) = altaria::profile::slot(name); \
    altaria::profile::Scope ALTARIA_PROFILE_JOIN(profile_scope_, __LINE__)(ALTARIA_PROFILE_JOIN(profile_slot_, __LINE__))
