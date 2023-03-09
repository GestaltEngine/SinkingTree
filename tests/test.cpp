#include "unordered_cc_map.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <iostream>
#include <random>
#include <string>
#include <unordered_map>

TEST_CASE("Basic") {
    {
        HashTree<int, int> map(16);
        REQUIRE(map.Put(1, 1));
        REQUIRE(map.Put(2, 2));
        REQUIRE(map.Put(3, 3));
        REQUIRE(!map.Put(1, 0));
        REQUIRE(!map.Put(2, 0));
        REQUIRE(!map.Put(3, 0));
        REQUIRE(map.Get(3).has_value());
        REQUIRE(map.Get(2).has_value());
        REQUIRE(map.Get(1).has_value());
        REQUIRE(map.Erase(3));
        REQUIRE(map.Erase(2));
        REQUIRE(map.Erase(1));
    }
}

TEST_CASE("Mix") {
    HashTree<int, int> my(16);
    std::unordered_map<int, int> baseline;
    std::mt19937 gen(0);
    std::uniform_int_distribution<int> dist;
    for (int i = 0; i < 1'000'000; ++i) {
        int key = dist(gen);
        int op = dist(gen);
        if (op % 10 < 2) {
            auto res = my.Put(key, i);
            auto base = baseline.insert_or_assign(key, i);
            REQUIRE(res == base.second);
        } else if (op % 10 < 4) {
            auto res = my.Erase(key);
            auto base = baseline.erase(key);
            REQUIRE(res == (base == 1));
        } else {
            auto res = my.Get(key);
            auto base = baseline.find(key);
            REQUIRE(res.has_value() == (base != baseline.end()));
        }
    }
}
