#include "core/kiotty_pool.h"
#include "core/kiotty_stage.h"

#include <benchmark/benchmark.h>

#include <cstring>

typedef kiotty::PoolLayout<kiotty::BlockClass<64, 1024>, kiotty::BlockClass<4096, 64>> BenchLayout;

static void PoolAllocateRelease(benchmark::State& state)
{
    kiotty::Pool pool(BenchLayout::classes(), BenchLayout::CLASS_COUNT);
    for (auto _ : state)
    {
        void* block = pool.allocate(64);
        benchmark::DoNotOptimize(block);
        pool.release(block);
    }
}
BENCHMARK(PoolAllocateRelease);

static void StageProvideConsume(benchmark::State& state)
{
    kiotty::Pool pool(BenchLayout::classes(), BenchLayout::CLASS_COUNT);
    kiotty::Stage stage(pool, 4096);
    char message[64] = {};
    for (auto _ : state)
    {
        char* target = stage.provider().prepare(sizeof(message));
        if (target == nullptr)
        {
            stage.compact();
            target = stage.provider().prepare(sizeof(message));
        }
        std::memcpy(target, message, sizeof(message));
        stage.provider().commit(sizeof(message));
        char* source = stage.consumer().peek();
        benchmark::DoNotOptimize(source);
        stage.consumer().commit(sizeof(message));
    }
}
BENCHMARK(StageProvideConsume);
