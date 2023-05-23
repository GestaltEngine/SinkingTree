#pragma once

#include <algorithm>
#include <atomic>
#include <array>
#include <vector>

template <typename T, size_t ProtectedPointersPerThread = 1, size_t MaxThreadCount = 64,
          size_t BatchCap = 2 * MaxThreadCount* ProtectedPointersPerThread>
class Hazard {
    static_assert(BatchCap > ProtectedPointersPerThread * MaxThreadCount,
                  "there must be more retireable pointers than there are protected ones to ensure "
                  "Lock-Freedom");

    struct ThreadState {
        std::array<std::atomic<T*>, ProtectedPointersPerThread> protected_pointers;
        std::array<T*, BatchCap> retired_pointers;
        size_t retired_count{0};
    };

    static inline thread_local ThreadState* registry = nullptr;

public:
    class Mutator;

    class Manager {
    public:
        Mutator MakeMutator() {
            if (registry == nullptr) {
                size_t thread_id = thread_counter_.fetch_add(1, std::memory_order_relaxed);
                if (thread_id >= MaxThreadCount) {
                    thread_counter_.fetch_sub(1, std::memory_order_relaxed);
                    throw std::runtime_error("Max thread count overflow " +
                                             std::to_string(thread_id));
                }
                registry = new ThreadState;
                thread_pointers_[thread_id].store(registry, std::memory_order_relaxed);
            }
            return Mutator(this, registry);
        }

        void Scan(ThreadState* retiring) {
            std::vector<void*> all_protected;

            size_t threads = thread_counter_.load(std::memory_order_relaxed);
            for (size_t i = 0; i < threads; ++i) {
                auto* ts = thread_pointers_[i].load(std::memory_order_relaxed);
                if (ts == nullptr) {
                    continue;
                }
                for (auto& atom_pointer : ts->protected_pointers) {
                    void* ptr = atom_pointer.load(std::memory_order_acquire);
                    if (ptr == nullptr) {
                        continue;
                    }
                    all_protected.push_back(ptr);
                }
            }

            std::sort(all_protected.begin(), all_protected.end());

            std::vector<T*> approved;
            std::vector<T*> dismissed;

            for (T* rptr : retiring->retired_pointers) {
                if (std::binary_search(all_protected.begin(), all_protected.end(), rptr)) {
                    dismissed.push_back(rptr);
                } else {
                    approved.push_back(rptr);
                }
            }

            for (T* rptr : approved) {
                delete rptr;
            }

            retiring->retired_count = 0;
            for (T* rptr : dismissed) {
                retiring->retired_pointers[retiring->retired_count++] = rptr;
            }
        }

        void Cleanup() {
            size_t threads = thread_counter_.load(std::memory_order_relaxed);
            for (size_t i = 0; i < threads; ++i) {
                ThreadState* ts = thread_pointers_[i].load(std::memory_order_relaxed);
                for (size_t j = 0; j < ts->retired_count; ++j) {
                    delete ts->retired_pointers[j];
                    ts->retired_pointers[j] = nullptr;
                }
                delete ts;
            }
            thread_counter_.store(0, std::memory_order_relaxed);
            registry = nullptr;
        }

        ~Manager() {
            Cleanup();
        }

    private:
        std::array<std::atomic<ThreadState*>, MaxThreadCount> thread_pointers_{};
        std::atomic<size_t> thread_counter_{0};
    };

    class Mutator {
        // V might be a child to T or void
        template <typename V>
        using AtomicPtr = std::atomic<V*>;

    public:
        explicit Mutator(Manager* manager, ThreadState* tstate)
            : manager_(manager), tstate_(tstate) {
        }

        template <typename V>
        T* Protect(size_t index, AtomicPtr<V>& ptr) {
            if (index >= ProtectedPointersPerThread) {
                throw std::runtime_error("bad index");
            }
            T* before;
            T* after = reinterpret_cast<T*>(ptr.load(std::memory_order_relaxed));
            do {
                before = after;
                tstate_->protected_pointers[index].store(before, std::memory_order_release);
                after = reinterpret_cast<T*>(ptr.load(std::memory_order_acquire));
            } while (after != before);
            return after;
        }

        void Retire(T* ptr) {
            tstate_->retired_pointers[tstate_->retired_count++] = ptr;
            if (tstate_->retired_count == BatchCap) {
                manager_->Scan(tstate_);
            }
        }

    private:
        Manager* manager_;
        ThreadState* tstate_;
    };
};
