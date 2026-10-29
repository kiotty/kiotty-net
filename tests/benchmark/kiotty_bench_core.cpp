#include "core/kiotty_pool.h"
#include "core/kiotty_stage.h"

#include <benchmark/benchmark.h>

#include <cstring>

static void PoolAllocateRelease(benchmark::State& state)
{
    kiotty::Pool pool(2 * 1024 * 1024);
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
    kiotty::Stage stage(4096);
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
