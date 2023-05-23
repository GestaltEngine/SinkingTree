#pragma once

#include <random>
#include <thread>
#include <vector>
#include <limits>
#include <unordered_set>
#include <forward_list>

enum class QueryType { INSERT, ERASE, FIND, CLEAR };

class Random {
public:
    explicit Random(uint32_t seed, int min = std::numeric_limits<int>::min(),
                    int max = std::numeric_limits<int>::max())
        : gen_{seed}, dist_{min, max} {
    }
    int operator()() {
        return dist_(gen_);
    }

private:
    std::mt19937 gen_;
    std::uniform_int_distribution<int> dist_;
};
