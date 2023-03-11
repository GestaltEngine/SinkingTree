#include "hazard_ptr.h"
#include "hashers.h"

#include <atomic>
#include <cassert>
#include <cstdint>
#include <functional>
#include <optional>

namespace {
constexpr HashType n_bit_mask(int n) {
    return (1 << n) - 1;
}

constexpr size_t power(int n) {
    return 1 << n;
}

uintptr_t bits(void *ptr) {
    return reinterpret_cast<uintptr_t>(ptr);
}

}  // namespace

using namespace hashers;
// declarations

enum class AcceptorState { kEmpty, kKeyValue, kCell };
enum class InjectorState { kEmpty, kKeyValue, kCell };

template <class Key, class Value, class Hasher = DefaultHasher<Key>>
class HashTree {
    typedef std::pair<Key, Value> KV;

    struct Root {
        size_t bit_count;
        std::atomic<void *> ptrs[];
    };

    struct Cell {
        std::atomic<void *> lhs{};
        std::atomic<void *> rhs{};
        // nullptr - std::pair<Key, Value>*, free
        // 1 lowest bit is 0 and not nullptr - std::pair<Key, Value>*, taken
        // 1 lowest bit is 1 - Cell*
        ~Cell();
    };

    struct TreeTraverser {
        HashType hash;
        int depth = -1;

        int advance(int bit_count = 1) {
            int index = hash & n_bit_mask(bit_count);
            hash = hash >> bit_count;
            depth++;
            return index;
        }
    };

public:
    HashTree(size_t capacity = 2, Hasher hasher = Hasher());
    bool Put(Key key, Value value);
    std::optional<Value> Get(const Key &key);
    bool Erase(const Key &key);
    ~HashTree();

    HashTree(const HashTree &other) = delete;
    HashTree operator=(const HashTree &other) = delete;
    HashTree(HashTree &&other) = delete;
    HashTree operator=(HashTree &&other) = delete;

private:
    std::pair<AcceptorState, InjectorState> deliberate_state(void *, void *);
    void free_root(Root *ptr);
    std::atomic<Root *> root_;
    Hasher hasher_;

    std::atomic<uint8_t>
        rehash_;  // acts as try-lock mutex, allowing only one thread to do rehashing at any time
    std::vector<Root *> old_roots_;
    // old roots aren't deleted in this version of the map,
    // ensuring memory safety at significant memory cost of
    // ~1.5 * elem_count * sizeof(void *) overhead
    std::atomic<size_t> cell_count_[8 * sizeof(HashType)];
};

// definitions

template <class Key, class Value, class Hasher>
void HashTree<Key, Value, Hasher>::free_root(Root *ptr) {
    for (size_t i = 0; i < power(ptr->bit_count); ++i) {
        if (bits(ptr->ptrs[i]) & 1) {
            HashTree<Key, Value, Hasher>::Cell *cptr =
                reinterpret_cast<Cell *>(bits(ptr->ptrs[i]) & ~7);
            delete cptr;
        } else if (ptr->ptrs[i] != nullptr) {
            KV *kv = reinterpret_cast<KV *>(ptr->ptrs[i].load());
            delete kv;
        }
    }
    free(ptr);
}

template <class Key, class Value, class Hasher>
HashTree<Key, Value, Hasher>::HashTree(size_t capacity, Hasher hasher)
    : hasher_(hasher), old_roots_(64, nullptr) {
    size_t bit_count = 1;
    size_t root_size = 2;
    while (root_size < capacity) {
        root_size <<= 1;
        bit_count++;
    }
    Root *r_ptr =
        reinterpret_cast<Root *>(malloc(sizeof(Root) + sizeof(std::atomic<void *>) * root_size));
    root_.store(r_ptr, std::memory_order_relaxed);
    r_ptr->bit_count = bit_count;
    for (size_t i = 0; i < root_size; ++i) {
        r_ptr->ptrs[i] = nullptr;
    }
}

template <class Key, class Value, class Hasher>
bool HashTree<Key, Value, Hasher>::Put(Key key, Value value) {
    TreeTraverser traverser{hasher_(key)};
    Root *root = root_.load(std::memory_order_relaxed);
    std::atomic<void *> *ptr2atomic = &(root->ptrs[traverser.advance(root->bit_count)]);

    void *desired = new KV(std::move(key), std::move(value));
    void *expected = ptr2atomic->load(std::memory_order_relaxed);

    int migration_index = 0;

    void *second_extra = nullptr;
    while (true) {
        do {
            auto [acc, inj] = deliberate_state(expected, desired);
            if (inj == InjectorState::kCell) {
                Release();
                Cell *discard = reinterpret_cast<Cell *>(bits(desired) & ~1);
                reinterpret_cast<std::atomic<void *> *>(discard)[migration_index].store(
                    nullptr, std::memory_order_relaxed);
                delete discard;
                desired = second_extra;
                second_extra = nullptr;
            }
            if (acc == AcceptorState::kKeyValue) {
                void *ptr = Acquire(ptr2atomic);
                if (bits(ptr) & 1) {
                    Release();
                    ptr2atomic = &(reinterpret_cast<std::atomic<void *> *>(
                        bits(ptr) & ~1)[traverser.advance()]);
                    expected = nullptr;
                } else {
                    KV *acc_ptr = reinterpret_cast<KV *>(ptr);
                    KV *inj_ptr = reinterpret_cast<KV *>(desired);
                    if (acc_ptr->first == inj_ptr->first) {
                        Release();
                        expected = ptr;
                        continue;
                    }
                    // WE DON'T RELEASE HERE, IT'S OMEGA-3 LEVEL OF IMPORTANT
                    Cell *new_cell = new Cell;
                    migration_index =
                        (hasher_(acc_ptr->first) >> (traverser.depth + root->bit_count)) & 1;
                    second_extra = desired;
                    reinterpret_cast<std::atomic<void *> *>(new_cell)[migration_index].store(
                        ptr, std::memory_order_relaxed);
                    desired = reinterpret_cast<void *>(bits(new_cell) | 1);
                    expected = ptr;
                }
            } else if (acc == AcceptorState::kCell) {
                ptr2atomic = &(reinterpret_cast<std::atomic<void *> *>(bits(expected) &
                                                                       ~1)[traverser.advance()]);
                expected = nullptr;
            } else {
                // do nothing, cell's empty, we can push
            }
        } while (!ptr2atomic->compare_exchange_weak(expected, desired, std::memory_order_relaxed));

        if (second_extra == nullptr) {
            break;
        } else {
            Release();
            cell_count_[root->bit_count + traverser.depth - 1].fetch_add(1);
            ptr2atomic =
                &(reinterpret_cast<std::atomic<void *> *>(bits(desired) & ~1)[traverser.advance()]);
            expected = nullptr;
            desired = second_extra;
            second_extra = nullptr;
        }
    }

    // cleanup the replaced KV pair if there is one
    if (expected != nullptr) {
        Retire(reinterpret_cast<KV *>(expected));
        return false;
    }
    return true;
}

template <class Key, class Value, class Hasher>
std::optional<Value> HashTree<Key, Value, Hasher>::Get(const Key &key) {
    Root *root = root_.load(std::memory_order_relaxed);
    TreeTraverser traverser{hasher_(key)};
    std::atomic<void *> *ptr2atomic = &(root->ptrs[traverser.advance(root->bit_count)]);
    void *ptr = ptr2atomic->load(std::memory_order_relaxed);

    while (true) {
        if (ptr == nullptr) {
            return std::nullopt;
        }
        if (bits(ptr) & 1) {
            ptr2atomic =
                &(reinterpret_cast<std::atomic<void *> *>(bits(ptr) & ~1)[traverser.advance()]);
            ptr = ptr2atomic->load(std::memory_order_relaxed);
            continue;
        }
        ptr = Acquire(ptr2atomic);
        if (ptr == nullptr) {
            Release();
            return std::nullopt;
        } else if (bits(ptr) & 1) {
            Release();
            ptr2atomic =
                &(reinterpret_cast<std::atomic<void *> *>(bits(ptr) & ~1)[traverser.advance()]);
            ptr = ptr2atomic->load(std::memory_order_relaxed);
            continue;
        }
        KV *kv = reinterpret_cast<KV *>(ptr);
        std::optional<Value> ret_val;
        if (kv->first == key) {
            ret_val = kv->second;
        }
        Release();
        return ret_val;
    }
}

template <class Key, class Value, class Hasher>
std::pair<AcceptorState, InjectorState> HashTree<Key, Value, Hasher>::deliberate_state(
    void *expected, void *desired) {
    AcceptorState acc;
    InjectorState inj;
    if (expected == nullptr) {
        acc = AcceptorState::kEmpty;
    } else if (bits(expected) & 1) {
        acc = AcceptorState::kCell;
    } else {
        acc = AcceptorState::kKeyValue;
    }
    if (bits(desired) & 1) {
        inj = InjectorState::kCell;
    } else if (desired == nullptr) {
        inj = InjectorState::kEmpty;
    } else {
        inj = InjectorState::kKeyValue;
    }
    return {acc, inj};
}

template <class Key, class Value, class Hasher>
bool HashTree<Key, Value, Hasher>::Erase(const Key &key) {
    TreeTraverser traverser{hasher_(key)};
    Root *root = root_.load(std::memory_order_relaxed);
    std::atomic<void *> *ptr2atomic = &(root->ptrs[traverser.advance(root->bit_count)]);
    void *ptr = ptr2atomic->load(std::memory_order_relaxed);

    while (true) {
        if (ptr == nullptr) {
            return false;
        }
        if (bits(ptr) & 1) {
            ptr2atomic =
                &(reinterpret_cast<std::atomic<void *> *>(bits(ptr) & ~1)[traverser.advance()]);
            ptr = ptr2atomic->load(std::memory_order_relaxed);
            continue;
        }
        ptr = Acquire(ptr2atomic);
        if (bits(ptr) & 1) {
            Release();
            ptr2atomic =
                &(reinterpret_cast<std::atomic<void *> *>(bits(ptr) & ~1)[traverser.advance()]);
            ptr = ptr2atomic->load(std::memory_order_relaxed);
            continue;
        } else if (ptr == nullptr) {
            Release();
            return false;
        } else {
            KV *kv = reinterpret_cast<KV *>(ptr);
            if (kv->first != key) {
                Release();
                return false;
            }
            bool cas_success =
                ptr2atomic->compare_exchange_strong(ptr, nullptr, std::memory_order_relaxed);
            if (cas_success) {
                Retire(kv);
                Release();
                return true;
            }
            Release();
        }
    }
}

template <class Key, class Value, class Hasher>
HashTree<Key, Value, Hasher>::Cell::~Cell() {
    if (bits(lhs) & 1) {
        delete reinterpret_cast<Cell *>(bits(lhs) & ~7UL);
    } else if (lhs != nullptr) {
        delete reinterpret_cast<KV *>(lhs.load());
    }
    if (bits(rhs) & 1) {
        delete reinterpret_cast<Cell *>(bits(rhs) & ~7UL);
    } else if (rhs != nullptr) {
        delete reinterpret_cast<KV *>(rhs.load());
    }
}

template <class Key, class Value, class Hasher>
HashTree<Key, Value, Hasher>::~HashTree() {
    for (Root *rptr : old_roots_) {
        if (rptr == nullptr) {
            continue;
        }
        for (size_t j = 0; j < power(rptr->bit_count); ++j) {
            rptr->ptrs[j] = nullptr;
        }
        free_root(rptr);
    }
    free_root(root_.load());
}
