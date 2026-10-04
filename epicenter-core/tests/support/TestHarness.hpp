#pragma once
// Minimal dependency-free test harness (no network fetch, no third-party code).
//
//   TEST_CASE(name) { CHECK(cond); CHECK_NEAR(a, b, tol); REQUIRE(cond); }
//   int main(int argc, char** argv) { return epicenter_test::runAll(argc, argv); }
//
// Optional argv[1] = substring filter on test names.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace epicenter_test {

struct TestCase {
    const char* name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

struct State {
    int failures = 0;
    const char* current = "";
};

inline State& state() {
    static State s;
    return s;
}

struct RequireFailed {};

inline void reportFailure(const char* file, int line, const std::string& message) {
    ++state().failures;
    std::fprintf(stderr, "  FAIL [%s] %s:%d: %s\n", state().current, file, line, message.c_str());
}

inline int runAll(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int run = 0;
    int failedTests = 0;
    for (const auto& t : registry()) {
        if (filter && std::strstr(t.name, filter) == nullptr) continue;
        state().current = t.name;
        const int before = state().failures;
        std::printf("[ RUN  ] %s\n", t.name);
        std::fflush(stdout);
        try {
            t.fn();
        } catch (const RequireFailed&) {
        } catch (const std::exception& e) {
            reportFailure(__FILE__, __LINE__, std::string("unexpected exception: ") + e.what());
        }
        ++run;
        const bool ok = state().failures == before;
        if (!ok) ++failedTests;
        std::printf("[ %s ] %s\n", ok ? " OK " : "FAIL", t.name);
        std::fflush(stdout);
    }
    std::printf("\n%d test(s) run, %d failed\n", run, failedTests);
    return failedTests == 0 && run > 0 ? 0 : 1;
}

} // namespace epicenter_test

#define EPI_CONCAT_INNER(a, b) a##b
#define EPI_CONCAT(a, b) EPI_CONCAT_INNER(a, b)

#define TEST_CASE(name)                                                                     \
    static void EPI_CONCAT(epi_test_fn_, __LINE__)();                                       \
    static ::epicenter_test::Registrar EPI_CONCAT(epi_test_reg_, __LINE__)(                 \
        name, &EPI_CONCAT(epi_test_fn_, __LINE__));                                         \
    static void EPI_CONCAT(epi_test_fn_, __LINE__)()

#define CHECK(cond)                                                                         \
    do { if (!(cond)) ::epicenter_test::reportFailure(__FILE__, __LINE__, "CHECK(" #cond ")"); } while (0)

#define CHECK_MSG(cond, msg)                                                                \
    do { if (!(cond)) ::epicenter_test::reportFailure(__FILE__, __LINE__, std::string(msg)); } while (0)

#define REQUIRE(cond)                                                                       \
    do {                                                                                    \
        if (!(cond)) {                                                                      \
            ::epicenter_test::reportFailure(__FILE__, __LINE__, "REQUIRE(" #cond ")");      \
            throw ::epicenter_test::RequireFailed{};                                        \
        }                                                                                   \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                               \
    do {                                                                                    \
        const double epi_a = static_cast<double>(a);                                        \
        const double epi_b = static_cast<double>(b);                                        \
        if (!(std::fabs(epi_a - epi_b) <= static_cast<double>(tol))) {                      \
            char epi_buf[256];                                                              \
            std::snprintf(epi_buf, sizeof(epi_buf), "CHECK_NEAR(%s, %s): %.9g vs %.9g (tol %.3g)", \
                          #a, #b, epi_a, epi_b, static_cast<double>(tol));                  \
            ::epicenter_test::reportFailure(__FILE__, __LINE__, epi_buf);                   \
        }                                                                                   \
    } while (0)
