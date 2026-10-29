#include <benchmark/benchmark.h>

#include "kiotty_net_statemachine.h"

static void MakeReadEntryAction(benchmark::State& state)
{
    for (auto _ : state)
    {
        KiottyNetEntryAction action = KiottyNet_makeReadEntryAction(4);
        benchmark::DoNotOptimize(action);
    }
}
BENCHMARK(MakeReadEntryAction);
