#include "commons.h"
#include "runner.h"
#include "unordered_cc_map.h"
#include <ranges>
#include "mutexed_std.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>


static constexpr auto kSeed = 14753334;

TEST_CASE( "Benchmark my map" ) {
    const auto kNumThreads = GENERATE(2u, 4u, 8u);
    static constexpr auto kNumIterations = 100'000;

    BENCHMARK("RandomInsertions:" + std::to_string(kNumThreads)) {
        HashTree<int, int> map(2 * kNumIterations);
        Runner runner{kNumIterations};
        for (auto i : std::views::iota(0u, kNumThreads)) {
            Random rand{kSeed + 10 * i};
            runner.Do([&map, rand]() mutable { map.Put(rand(), 1); });
        }
    };

    BENCHMARK("RandomInsertions(std):" + std::to_string(kNumThreads)) {
        Baseline<int, int> map(2 * kNumIterations);
        Runner runner{kNumIterations};
        for (auto i : std::views::iota(0u, kNumThreads)) {
            Random rand{kSeed + 10 * i};
            runner.Do([&map, rand]() mutable { map.Put(rand(), 1); });
        }
    };
}
