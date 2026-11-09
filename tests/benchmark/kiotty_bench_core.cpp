#include "core/kiotty_stage.h"

#include <benchmark/benchmark.h>

#include <cstring>

static void StageProvideConsume(benchmark::State& state)
{
    kiotty::net::implementation::Stage stage{ 4096 };
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
