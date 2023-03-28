#include "unordered_cc_map.h"
#include "runner.h"
#include "commons.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <ranges>

using namespace sinking_tree;

TEST_CASE("Multistress") {
    SinkingTree<int, int> my(16);
    const auto kNumThreads = GENERATE(2u, 4u, 8u);

    const int kNumIterations = 1'000'000;

    Runner runner{kNumIterations};
    for (auto i : std::views::iota(0u, kNumThreads)) {
        Random rand{i};
        runner.Do([&my, rand]() mutable {
            auto choice = rand() % 100;
            if (choice < 20) {
                my.Put(rand(), 1);
            } else if (choice < 40) {
                my.Erase(rand());
            } else {
                my.Get(rand());
            }
        });
    }
}
