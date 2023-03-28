#pragma once

#include <atomic>
#include <cassert>
#include <memory>
#include <functional>
#include <mutex>
#include <unordered_set>
#include <iostream>

namespace {

struct ThreadState {
    std::atomic<void*>* ptr;
};

struct RetiredPtr {
    void* value;
    std::function<void()> deleter;
    RetiredPtr* next;
};

thread_local std::atomic<void*> hazard_ptr{nullptr};  // currently captured pointer
thread_local ThreadState* registry = nullptr;

struct HazardPtrGlobalState {
    std::mutex threads_lock;
    std::unordered_set<ThreadState*> threads;  // pointers to pointers to protected atomic pointers

    std::mutex scan_lock;
    std::atomic<RetiredPtr*> free_list = nullptr;
    std::atomic<int> free_list_size_approx = 0;
    static const int kMaxFreeListLength = 100;

    ~HazardPtrGlobalState() {
        RetiredPtr* retired = free_list.load();
        while (retired != nullptr) {
            retired->deleter();
            auto* tmp = retired;
            retired = retired->next;
            delete tmp;
        }
    }
};

HazardPtrGlobalState global;

void ScanFreeList() {
    int fls = global.free_list_size_approx.exchange(0);
    if (!global.scan_lock.try_lock()) {
        return;
    }
    if (fls <= global.kMaxFreeListLength) {
        global.scan_lock.unlock();
        global.free_list_size_approx.fetch_add(fls);
        return;
    }
    auto retired = global.free_list.exchange(nullptr);
    std::unordered_set<void*> hazard;
    {
        std::lock_guard guard(global.threads_lock);
        for (const auto& thread : global.threads) {
            if (auto ptr = thread->ptr->load(); ptr) {
                hazard.insert(ptr);
            }
        }
    }

    while (retired != nullptr) {
        auto next = retired->next;
        if (hazard.count(retired->value) > 0) {
            auto old_free_list = global.free_list.load();
            retired->next = old_free_list;
            while (!global.free_list.compare_exchange_weak(old_free_list, retired)) {
                retired->next = old_free_list;
            }
            global.free_list_size_approx.fetch_add(1);
        } else {
            retired->deleter();
            delete retired;
        }
        retired = next;
    }
    global.scan_lock.unlock();
}

}  // namespace

namespace hazard {

template <class T>
T* Acquire(std::atomic<T*>* ptr) {
    assert(registry != nullptr);
    auto value = ptr->load();
    do {
        hazard_ptr.store(value);

        auto new_value = ptr->load();
        if (new_value == value) {
            return value;
        }

        value = new_value;
    } while (true);
}

inline void Release() {
    assert(registry != nullptr);
    hazard_ptr.store(nullptr);
}

template <class T, class Deleter = std::default_delete<T>>
void Retire(T* value, Deleter deleter = {}) {
    assert(registry != nullptr);
    auto old_free_list = global.free_list.load();
    auto rptr = new RetiredPtr;
    rptr->value = value;
    rptr->deleter = [value, deleter]() { deleter(value); };
    rptr->next = old_free_list;

    while (!global.free_list.compare_exchange_weak(old_free_list, rptr)) {
        rptr->next = old_free_list;
    }

    int size = global.free_list_size_approx.fetch_add(1);
    if (size > global.kMaxFreeListLength) {
        ScanFreeList();
    }
}

inline void RegisterThread() {
    std::lock_guard<std::mutex> guard(global.threads_lock);
    if (registry != nullptr) {
        throw std::runtime_error("RegisterThread called before UnregisterThread");
    }
    registry = new ThreadState;
    registry->ptr = &hazard_ptr;
    global.threads.insert(registry);
}

inline void UnregisterThread() {
    std::lock_guard<std::mutex> guard(global.threads_lock);
    if (registry == nullptr) {
        throw std::runtime_error("UnregisterThread called before RegisterThread");
    }
    global.threads.erase(registry);
    delete registry;
    registry = nullptr;
}

}  // namespace hazard
