#include <benchmark/benchmark.h>

#include "kiotty_net_statemachine.h"

static void MoveToNextState(benchmark::State& state)
{
    KiottyNetConnectionAction action = {};
    action.type = KIOTTY_ACTION_READ;
    action.as.read.exact = 4;
    for (auto _ : state)
    {
        KiottyNetTransition transition = KiottyNet_moveToNextState(1, action);
        benchmark::DoNotOptimize(transition);
    }
}
BENCHMARK(MoveToNextState);
