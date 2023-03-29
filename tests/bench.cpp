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
    static constexpr auto kNumIterations = 100'000;
    for (uint thread_count = 1; thread_count <= 8; thread_count *= 2) {

        BENCHMARK_ADVANCED("RandomInsertions:" + std::to_string(thread_count))
        (Catch::Benchmark::Chronometer meter) {
            SinkingTree<int, int> map(kNumIterations);
            meter.measure([thread_count, &map]() {
                Runner runner{kNumIterations};
                for (auto i : std::views::iota(0u, thread_count)) {
                    Random rand{kSeed + 10 * i};
                    runner.Do([&map, rand]() mutable { map.Put(rand(), 1); });
                }
            });
        };

        BENCHMARK_ADVANCED("RandomInsertions(std):" + std::to_string(thread_count))
        (Catch::Benchmark::Chronometer meter) {
            Baseline<int, int> map(kNumIterations);
            meter.measure([thread_count, &map]() {
                Runner runner{kNumIterations};
                for (auto i : std::views::iota(0u, thread_count)) {
                    Random rand{kSeed + 10 * i};
                    runner.Do([&map, rand]() mutable { map.Put(rand(), 1); });
                }
            });
        };
    }
}

TEST_CASE("Benchmark inserts from default size") {
    static constexpr auto kNumIterations = 100'000;
    for (uint thread_count = 1; thread_count <= 8; thread_count *= 2) {
        BENCHMARK_ADVANCED("RandomInsertions:" + std::to_string(thread_count))
        (Catch::Benchmark::Chronometer meter) {
            SinkingTree<int, int> map;
            meter.measure([thread_count, &map]() {
                Runner runner{kNumIterations};
                for (auto i : std::views::iota(0u, thread_count)) {
                    Random rand{kSeed + 10 * i};
                    runner.Do([&map, rand]() mutable { map.Put(rand(), 1); });
                }
            });
        };

        BENCHMARK_ADVANCED("RandomInsertions(std):" + std::to_string(thread_count))
        (Catch::Benchmark::Chronometer meter) {
            Baseline<int, int> map;
            meter.measure([thread_count, &map]() {
                Runner runner{kNumIterations};
                for (auto i : std::views::iota(0u, thread_count)) {
                    Random rand{kSeed + 10 * i};
                    runner.Do([&map, rand]() mutable { map.Put(rand(), 1); });
                }
            });
        };
    }
}

TEST_CASE("Benchmark reads") {
    const auto kSize = 1'000;
    static constexpr auto kNumIterations = 100'000;
    for (uint thread_count = 1; thread_count <= 8; thread_count *= 2) {

        BENCHMARK_ADVANCED("RandomReads: " + std::to_string(thread_count) + ", " +
                           std::to_string(kSize))
        (Catch::Benchmark::Chronometer meter) {
            SinkingTree<int, int> map(kNumIterations);
            hazard::RegisterThread();
            for (int i = 0; i < kSize; ++i) {
                map.Put(i, i);
            }
            hazard::UnregisterThread();
            meter.measure([thread_count, &map]() {
                Runner runner{kNumIterations};
                for (auto i : std::views::iota(0u, thread_count)) {
                    Random rand{kSeed + 10 * i};
                    runner.Do([&map, rand]() mutable { map.Get(rand()); });
                }
            });
        };

        BENCHMARK_ADVANCED("RandomReads(std):" + std::to_string(thread_count) + ", " +
                           std::to_string(kSize))
        (Catch::Benchmark::Chronometer meter) {
            Baseline<int, int> map(kNumIterations);
            for (int i = 0; i < kSize; ++i) {
                map.Put(i, i);
            }
            meter.measure([thread_count, &map]() {
                Runner runner{kNumIterations};
                for (auto i : std::views::iota(0u, thread_count)) {
                    Random rand{kSeed + 10 * i};
                    runner.Do([&map, rand]() mutable { map.Get(rand()); });
                }
            });
        };
    }
}
