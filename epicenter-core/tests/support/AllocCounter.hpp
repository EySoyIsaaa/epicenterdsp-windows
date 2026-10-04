#pragma once
// Heap-allocation tracking for real-time-safety tests.
//
// AllocCounter.cpp replaces the global operator new/delete family for the test executable.
// Tracking is per-thread: only allocations made by the thread that called beginTracking()
// are counted, so a concurrent control thread or the harness itself cannot pollute the result.
//
// NOTE: This only observes allocations made through operator new from code linked into the
// same module. The Core is linked statically into the tests, so every allocation the Core
// could perform (std::vector, new, std::string, ...) is observed. When building with
// AddressSanitizer the hook is compiled out (ASan owns the allocator) and available() is false.

#include <cstdint>

namespace epicenter_test::alloc {

struct Snapshot {
    std::uint64_t allocations = 0;
    std::uint64_t deallocations = 0;
    std::uint64_t bytes = 0;
};

bool available();
void beginTracking();      // resets and enables counting on the calling thread
Snapshot endTracking();    // disables counting on the calling thread and returns the counts
Snapshot processTotals();  // process-wide totals since start (all threads)

} // namespace epicenter_test::alloc
