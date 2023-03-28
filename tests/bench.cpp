#include "commons.h"
#include "runner.h"
#include "unordered_cc_map.h"
#include <ranges>
#include "mutexed_std.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

static constexpr auto kSeed = 14753334;

using namespace sinking_tree;

TEST_CASE("Benchmark inserts") {
    const auto kNumThreads = GENERATE(1u, 2u, 4u, 8u);
    static constexpr auto kNumIterations = 100'000;

    BENCHMARK("RandomInsertions:" + std::to_string(kNumThreads)) {
        SinkingTree<int, int> map(kNumIterations);
        Runner runner{kNumIterations};
        for (auto i : std::views::iota(0u, kNumThreads)) {
            Random rand{kSeed + 10 * i};
            runner.Do([&map, rand]() mutable { map.Put(rand(), 1); });
        }
    };

    BENCHMARK("RandomInsertions(std):" + std::to_string(kNumThreads)) {
        Baseline<int, int> map(kNumIterations);
        Runner runner{kNumIterations};
        for (auto i : std::views::iota(0u, kNumThreads)) {
            Random rand{kSeed + 10 * i};
            runner.Do([&map, rand]() mutable { map.Put(rand(), 1); });
        }
    };
}


TEST_CASE("Benchmark inserts from default size") {
    const auto kNumThreads = GENERATE(1u, 2u, 4u, 8u);
    static constexpr auto kNumIterations = 100'000;

    BENCHMARK("RandomInsertions:" + std::to_string(kNumThreads)) {
        SinkingTree<int, int> map;
        Runner runner{kNumIterations};
        for (auto i : std::views::iota(0u, kNumThreads)) {
            Random rand{kSeed + 10 * i};
            runner.Do([&map, rand]() mutable { map.Put(rand(), 1); });
        }
    };

    BENCHMARK("RandomInsertions(std):" + std::to_string(kNumThreads)) {
        Baseline<int, int> map;
        Runner runner{kNumIterations};
        for (auto i : std::views::iota(0u, kNumThreads)) {
            Random rand{kSeed + 10 * i};
            runner.Do([&map, rand]() mutable { map.Put(rand(), 1); });
        }
    };
}

TEST_CASE("Benchmark reads") {
    const auto kNumThreads = GENERATE(1u, 2u, 4u, 8u);
    const auto kSize = 1'000;
    static constexpr auto kNumIterations = 100'000;

    BENCHMARK("RandomReads: " + std::to_string(kNumThreads) + ", " + std::to_string(kSize)) {
        SinkingTree<int, int> map(kNumIterations);
        RegisterThread();

        for (int i = 0; i < kSize; ++i) {
            map.Put(i, i);
        }
        UnregisterThread();
        Runner runner{kNumIterations};
        for (auto i : std::views::iota(0u, kNumThreads)) {
            Random rand{kSeed + 10 * i};
            runner.Do([&map, rand]() mutable { map.Get(rand()); });
        }
    };

    BENCHMARK("RandomReads(std):" + std::to_string(kNumThreads) + ", " +
              std::to_string(kSize)) {
        Baseline<int, int> map(kNumIterations);

        for (int i = 0; i < kSize; ++i) {
            map.Put(i, i);
        }
        Runner runner{kNumIterations};
        for (auto i : std::views::iota(0u, kNumThreads)) {
            Random rand{kSeed + 10 * i};
            runner.Do([&map, rand]() mutable { map.Get(rand()); });
        }
    };
}
