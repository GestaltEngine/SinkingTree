#pragma once

#include "hazard_ptr.h"

#include <atomic>
#include <vector>
#include <thread>

using namespace hazard;

class Runner {
public:
    explicit Runner(uint64_t num_iterations) : num_iterations_{num_iterations} {
    }

    template <class Function, class... Args>
    void Do(Function&& func, Args&&... args) {
        threads_.emplace_back(
            [this](Function&& func, Args&&... args) mutable {
                RegisterThread();
                while (iteration_.fetch_add(100, std::memory_order::relaxed) < num_iterations_) {
                    for (int i = 0; i < 100; ++i) {
                        func(std::forward<Args>(args)...);
                    }
                }
                UnregisterThread();
            },
            std::forward<Function>(func), std::forward<Args>(args)...);
    }

    void Wait() {
        threads_.clear();
    }

private:
    const uint64_t num_iterations_;
    std::atomic<uint64_t> iteration_;
    std::vector<std::jthread> threads_;
};
