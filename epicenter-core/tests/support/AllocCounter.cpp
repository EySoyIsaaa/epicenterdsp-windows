#include "AllocCounter.hpp"

#include <atomic>
#include <cstdlib>
#include <new>

namespace {
std::atomic<std::uint64_t> g_allocs{0};
std::atomic<std::uint64_t> g_frees{0};
std::atomic<std::uint64_t> g_bytes{0};

thread_local bool t_tracking = false;
thread_local std::uint64_t t_allocs = 0;
thread_local std::uint64_t t_frees = 0;
thread_local std::uint64_t t_bytes = 0;

#if !defined(EPICENTER_TEST_NO_ALLOC_HOOK)
inline void noteAlloc(std::size_t n) {
    g_allocs.fetch_add(1, std::memory_order_relaxed);
    g_bytes.fetch_add(n, std::memory_order_relaxed);
    if (t_tracking) { ++t_allocs; t_bytes += n; }
}
inline void noteFree(void* p) {
    if (!p) return;
    g_frees.fetch_add(1, std::memory_order_relaxed);
    if (t_tracking) ++t_frees;
}
void* rawAlloc(std::size_t n) {
    noteAlloc(n);
    return std::malloc(n ? n : 1);
}
void* rawAlignedAlloc(std::size_t n, std::size_t align) {
    noteAlloc(n);
#if defined(_WIN32)
    return _aligned_malloc(n ? n : 1, align);
#else
    const std::size_t rounded = ((n ? n : 1) + align - 1) / align * align;
    return std::aligned_alloc(align, rounded);
#endif
}
void rawAlignedFree(void* p) {
    noteFree(p);
#if defined(_WIN32)
    _aligned_free(p);
#else
    std::free(p);
#endif
}
#endif
} // namespace

namespace epicenter_test::alloc {

bool available() {
#if defined(EPICENTER_TEST_NO_ALLOC_HOOK)
    return false;
#else
    return true;
#endif
}

void beginTracking() {
    t_allocs = t_frees = t_bytes = 0;
    t_tracking = true;
}

Snapshot endTracking() {
    t_tracking = false;
    return {t_allocs, t_frees, t_bytes};
}

Snapshot processTotals() {
    return {g_allocs.load(), g_frees.load(), g_bytes.load()};
}

} // namespace epicenter_test::alloc

#if !defined(EPICENTER_TEST_NO_ALLOC_HOOK)
void* operator new(std::size_t n) { if (void* p = rawAlloc(n)) return p; throw std::bad_alloc(); }
void* operator new[](std::size_t n) { if (void* p = rawAlloc(n)) return p; throw std::bad_alloc(); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { return rawAlloc(n); }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { return rawAlloc(n); }
void operator delete(void* p) noexcept { noteFree(p); std::free(p); }
void operator delete[](void* p) noexcept { noteFree(p); std::free(p); }
void operator delete(void* p, std::size_t) noexcept { noteFree(p); std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { noteFree(p); std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { noteFree(p); std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { noteFree(p); std::free(p); }

void* operator new(std::size_t n, std::align_val_t a) { if (void* p = rawAlignedAlloc(n, static_cast<std::size_t>(a))) return p; throw std::bad_alloc(); }
void* operator new[](std::size_t n, std::align_val_t a) { if (void* p = rawAlignedAlloc(n, static_cast<std::size_t>(a))) return p; throw std::bad_alloc(); }
void* operator new(std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept { return rawAlignedAlloc(n, static_cast<std::size_t>(a)); }
void* operator new[](std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept { return rawAlignedAlloc(n, static_cast<std::size_t>(a)); }
void operator delete(void* p, std::align_val_t) noexcept { rawAlignedFree(p); }
void operator delete[](void* p, std::align_val_t) noexcept { rawAlignedFree(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { rawAlignedFree(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { rawAlignedFree(p); }
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept { rawAlignedFree(p); }
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept { rawAlignedFree(p); }
#endif
