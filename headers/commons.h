#pragma once

#include <random>
#include <thread>
#include <vector>
#include <limits>
#include <unordered_set>
#include <forward_list>

enum class QueryType { INSERT, ERASE, FIND, CLEAR };

class Increment {
public:
    explicit Increment(int start) : start_{start} {
    }
    int operator()() {
        return start_++;
    }

private:
    int start_;
};

class EqualLowBits {
public:
    explicit EqualLowBits(int low_bits_count) : low_bits_count_{low_bits_count} {
    }
    int operator()() {
        auto result = ((start_++) << low_bits_count_) + low_bits_;
        if (start_ == (1 << (32 - low_bits_count_))) {
            start_ = 0;
            ++low_bits_;
        }
        return result;
    }

private:
    int start_ = 0;
    int low_bits_ = 0;
    const int low_bits_count_;
};

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
