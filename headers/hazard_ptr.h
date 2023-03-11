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
};  // mb deque would be bettererester?


thread_local std::atomic<void*> hazard_ptr{nullptr};  // currently captured pointer
thread_local ThreadState* registry = nullptr;

std::mutex threads_lock;
std::unordered_set<ThreadState*> threads;  // pointers to pointers to protected atomic pointers

std::mutex scan_lock; 
std::atomic<RetiredPtr*> free_list = nullptr;
std::atomic<int> free_list_size_approx = 0;
const int kMaxFreeListLength = 100;

}  // namespace

// Acquire атомарно читает значение *ptr и добавляет это значение
// в множество защищённых указателей.
//
// Один поток в один момент времени может держать только один указатель в множестве защищённых.
//
// Объект, на который ссылается std::atomic<T*> должен быть удалён с использованием Retire. Нельзя
// удалять этот объект напрямую через delete.
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

// Release удаляет текущий активный указатель из множества защищённых.
inline void Release() {
    assert(registry != nullptr);
    hazard_ptr.store(nullptr);
}

void ScanFreeList() {
    free_list_size_approx.store(0);
    if (!scan_lock.try_lock()) {
        return;
    }
    if (free_list_size_approx.load() <= kMaxFreeListLength) {
        scan_lock.unlock();
        return;
    }
    auto retired = free_list.exchange(nullptr);
    std::unordered_set<void*> hazard;
    {
        std::unique_lock guard(threads_lock);
        for (const auto& thread : threads) {
            if (auto ptr = thread->ptr->load(); ptr) {
                hazard.insert(ptr);
            }
        }
    }

    while (retired != nullptr) {
        auto next = retired->next;
        if (hazard.count(retired->value) > 0) {
            auto old_free_list = free_list.load();
            while (!free_list.compare_exchange_weak(old_free_list, retired)) {
                retired->next = old_free_list;
            }
            free_list_size_approx++;
        } else {
            retired->deleter();
            delete retired;
        }
        retired = next;
    }
    scan_lock.unlock();
}

// Retire отправляет объект в очередь на удаление.
//
// Объект будет удалён в произвольный момент времени в будущем. Гарантируется, что удаление не
// произойдёт, пока объект находится в множестве защищённых.
//
// Пользователь должен гарантировать, никакой другой поток уже не имеет доступа к
// *value через атомарные переменные.
template <class T, class Deleter = std::default_delete<T>>
void Retire(T* value, Deleter deleter = {}) {
    assert(registry != nullptr);
    auto old_free_list = free_list.load();
    auto rptr = new RetiredPtr;
    rptr->value = value;
    rptr->deleter = [value, deleter]() { deleter(value); };  // type erasure, sorta
    rptr->next = old_free_list;

    while (!free_list.compare_exchange_weak(old_free_list, rptr)) {
        rptr->next = old_free_list;
    }
    free_list_size_approx++;

    int size = free_list_size_approx.load();
    if (size > kMaxFreeListLength) {
        ScanFreeList();
    }
}

// Каждый поток обязан позвать RegisterThread перед работой с Hazard Ptr.
inline void RegisterThread() {
    std::lock_guard<std::mutex> guard(threads_lock);
    if (registry != nullptr) {
        throw std::runtime_error("RegisterThread called before UnregisterThread");
    }
    registry = new (ThreadState);
    registry->ptr = &hazard_ptr;
    threads.insert(registry);
}

// UnregisterThread вызывается каждым потоком перед завершением. Работать с Hazard Ptr после вызова
// UnregisterThread нельзя.
inline void UnregisterThread() {
    std::lock_guard<std::mutex> guard(threads_lock);
    if (registry == nullptr) {
        throw std::runtime_error("UnregisterThread called before RegisterThread");
    }
    threads.erase(registry);
    delete registry;
    registry = nullptr;
}
