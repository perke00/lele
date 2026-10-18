// Real-time safety tests: proves audio-path code performs zero heap allocations.
// Works by replacing global operator new with a counting version for this executable.

#include "TestHarness.h"
#include "TestSignals.h"

#include "../src/dsp/YinEstimator.h"

#include <atomic>
#include <cstdlib>
#include <new>

namespace {
std::atomic<long> g_allocCount{ 0 };
std::atomic<bool> g_tracking{ false };

void* countedAlloc(std::size_t n)
{
    if (g_tracking.load(std::memory_order_relaxed))
        g_allocCount.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(n ? n : 1))
        return p;
    throw std::bad_alloc();
}
} // namespace

void* operator new(std::size_t n)   { return countedAlloc(n); }
void* operator new[](std::size_t n) { return countedAlloc(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

using namespace atlas;

ATLAS_TEST(realtime_alloc_counter_actually_works)
{
    // Guard against a vacuous test: the counter must see a deliberate allocation.
    g_allocCount = 0; g_tracking = true;
    auto* p = new int[16];
    g_tracking = false;
    delete[] p;
    CHECK(g_allocCount.load() >= 1);
}

ATLAS_TEST(realtime_yin_process_does_not_allocate)
{
    dsp::YinEstimator yin;
    yin.prepare(4000.0, 28.0, 400.0); // allocation allowed here
    auto sig = test::makeSine(4000.0, 60.0, 0.5, 0.8f);
    CHECK(static_cast<int>(sig.size()) >= yin.frameSize());

    g_allocCount = 0; g_tracking = true;
    float acc = 0.0f;
    for (int i = 0; i < 50; ++i)
        acc += yin.process(sig.data() + i).frequencyHz;
    g_tracking = false;

    INFO("allocations during 50 process() calls: %ld (acc=%.1f)", g_allocCount.load(), acc);
    CHECK(g_allocCount.load() == 0);
    CHECK(acc > 0.0f); // result is used, so the loop can't be optimised away
}
