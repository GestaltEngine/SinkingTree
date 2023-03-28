#include "hazard_ptr.h"
#include "hashers.h"

#include <atomic>
#include <cassert>
#include <cstdint>
#include <functional>
#include <optional>

namespace {
constexpr HashType n_bit_mask(int n) {
    return (static_cast<HashType>(1) << n) - 1;
}

constexpr size_t power(int n) {
    return static_cast<size_t>(1) << n;
}

uintptr_t bits(void *ptr) {
    return reinterpret_cast<uintptr_t>(ptr);
}

uintptr_t filter_ptr(void *ptr) {
    return bits(ptr) & ~static_cast<uintptr_t>(1);
}
}  // namespace

using namespace hashers;
// declarations

enum class AcceptorState { kEmpty, kKeyValue, kCell };
enum class InjectorState { kEmpty, kKeyValue, kCell };

template <class Key, class Value, class Hasher = DefaultHasher<Key>>
class HashTree {
    struct Root {
        size_t bit_count;
        std::atomic<void *> ptrs[];
    };

    struct alignas(16) Cell {
        std::atomic<void *> lhs{};
        std::atomic<void *> rhs{};
        // nullptr - std::pair<Key, Value>*, free
        // 1 lowest bit is 0 and not nullptr - std::pair<Key, Value>*, taken
        // 1 lowest bit is 1 - Cell*
        ~Cell();
    };

    // alignment property ensures last pointer bit for this type is always zero
    struct alignas(std::max(2UL, alignof(std::pair<Key, Value>))) KV {
        Key key;
        Value value;
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

    static constexpr int kMaxSolidity_ = 8 * sizeof(HashType);

public:
    HashTree(size_t capacity = 2, Hasher hasher = Hasher());
    ~HashTree();

    bool Put(const Key& key, const Value& value);
    std::optional<Value> Get(const Key &key);
    bool Erase(const Key &key);

    void MergeRoot(int);

    HashTree(const HashTree &other) = delete;
    HashTree operator=(const HashTree &other) = delete;
    HashTree(HashTree &&other) = delete;
    HashTree operator=(HashTree &&other) = delete;

private:
    AcceptorState deliberate_state(void *);
    void free_root(Root *);
    std::atomic<Root *> root_;
    Hasher hasher_;

    std::array<Root *, kMaxSolidity_> old_roots_{};
    std::atomic<size_t> cell_count_[kMaxSolidity_]{};
};

// definitions

template <class Key, class Value, class Hasher>
HashTree<Key, Value, Hasher>::HashTree(size_t capacity, Hasher hasher)
    : hasher_(hasher) {
    size_t bit_count = 1;
    size_t root_size = 2;
    while (root_size < capacity) {
        root_size <<= 1;
        bit_count++;
    }
    Root *r_ptr =
        reinterpret_cast<Root *>(malloc(sizeof(Root) + sizeof(std::atomic<void *>) * root_size));
    root_.store(r_ptr, std::memory_order_release);
    r_ptr->bit_count = bit_count;
    for (size_t i = 0; i < root_size; ++i) {
        r_ptr->ptrs[i] = nullptr;
    }
}

template <class Key, class Value, class Hasher>
bool HashTree<Key, Value, Hasher>::Put(const Key& key, const Value& value) {
    TreeTraverser traverser{hasher_(key)};
    Root *root = root_.load(std::memory_order_acquire);
    std::atomic<void *> *ptr2atomic = &(root->ptrs[traverser.advance(root->bit_count)]);

    void *desired = new KV{key, value};
    void *expected = ptr2atomic->load(std::memory_order_acquire);

    int migration_index = 0;

    void *second_extra = nullptr;
    InjectorState inj = InjectorState::kKeyValue;
    while (true) {
        do {
            auto acc = deliberate_state(expected);
            if (inj == InjectorState::kCell) {
                Release();
                Cell *discard = reinterpret_cast<Cell *>(filter_ptr(desired));
                reinterpret_cast<std::atomic<void *> *>(discard)[migration_index].store(
                    nullptr, std::memory_order_release);
                delete discard;
                desired = second_extra;
                second_extra = nullptr;
                inj = InjectorState::kKeyValue;
            }
            if (acc == AcceptorState::kKeyValue) {
                void *ptr = Acquire(ptr2atomic);
                if (bits(ptr) & 1) {
                    Release();
                    ptr2atomic = &(reinterpret_cast<std::atomic<void *> *>(
                        filter_ptr(ptr))[traverser.advance()]);
                    expected = nullptr;
                } else {
                    KV *acc_ptr = reinterpret_cast<KV *>(ptr);
                    KV *inj_ptr = reinterpret_cast<KV *>(desired);
                    if (acc_ptr->key == inj_ptr->key) {
                        Release();
                        expected = ptr;
                        continue;
                    }
                    // no Release() intended
                    Cell *new_cell = new Cell;
                    migration_index =
                        (hasher_(acc_ptr->key) >> (traverser.depth + root->bit_count)) & 1;
                    second_extra = desired;
                    reinterpret_cast<std::atomic<void *> *>(new_cell)[migration_index].store(
                        ptr, std::memory_order_release);
                    desired = reinterpret_cast<void *>(bits(new_cell) | 1);
                    expected = ptr;
                    inj = InjectorState::kCell;
                }
            } else if (acc == AcceptorState::kCell) {
                ptr2atomic = &(reinterpret_cast<std::atomic<void *> *>(filter_ptr(expected))[traverser.advance()]);
                expected = nullptr;
            } else {
                // inaction intended
            }
        } while (!ptr2atomic->compare_exchange_weak(expected, desired, std::memory_order_acq_rel));

        if (second_extra == nullptr) {
            break;
        } else {
            Release();
            int solidity = root->bit_count + traverser.depth;
            if (solidity <= kMaxSolidity_) {
                auto before = cell_count_[solidity - 1].fetch_add(1);
                if (solidity > 1 && before + 1 == power(solidity) && traverser.depth > 1) {
                    MergeRoot(solidity);
                }
            }
            ptr2atomic =
                &(reinterpret_cast<std::atomic<void *> *>(filter_ptr(desired))[traverser.advance()]);
            expected = nullptr;
            desired = second_extra;
            second_extra = nullptr;
            inj = InjectorState::kKeyValue;
        }
    }

    // cleanup the replaced KV if there is one
    if (expected != nullptr) {
        Retire(reinterpret_cast<KV *>(expected));
        return false;
    }
    return true;
}

template <class Key, class Value, class Hasher>
std::optional<Value> HashTree<Key, Value, Hasher>::Get(const Key &key) {
    Root *root = root_.load(std::memory_order_acquire);
    TreeTraverser traverser{hasher_(key)};
    std::atomic<void *> *ptr2atomic = &(root->ptrs[traverser.advance(root->bit_count)]);
    void *ptr = ptr2atomic->load(std::memory_order_acquire);

    while (true) {
        if (ptr == nullptr) {
            return std::nullopt;
        }
        if (bits(ptr) & 1) {
            ptr2atomic =
                &(reinterpret_cast<std::atomic<void *> *>(filter_ptr(ptr))[traverser.advance()]);
            ptr = ptr2atomic->load(std::memory_order_acquire);
            continue;
        }
        ptr = Acquire(ptr2atomic);
        if (ptr == nullptr) {
            Release();
            return std::nullopt;
        } else if (bits(ptr) & 1) {
            Release();
            ptr2atomic =
                &(reinterpret_cast<std::atomic<void *> *>(filter_ptr(ptr))[traverser.advance()]);
            ptr = ptr2atomic->load(std::memory_order_acquire);
            continue;
        }
        KV *kv = reinterpret_cast<KV *>(ptr);
        std::optional<Value> ret_val;
        if (kv->key == key) {
            ret_val = kv->value;
        }
        Release();
        return ret_val;
    }
}

template <class Key, class Value, class Hasher>
AcceptorState HashTree<Key, Value, Hasher>::deliberate_state(
    void *expected) {
    if (expected == nullptr) {
        return AcceptorState::kEmpty;
    } else if (bits(expected) & 1) {
        return AcceptorState::kCell;
    } else {
        return AcceptorState::kKeyValue;
    }
}

template <class Key, class Value, class Hasher>
bool HashTree<Key, Value, Hasher>::Erase(const Key &key) {
    TreeTraverser traverser{hasher_(key)};
    Root *root = root_.load(std::memory_order_acquire);
    std::atomic<void *> *ptr2atomic = &(root->ptrs[traverser.advance(root->bit_count)]);
    void *ptr = ptr2atomic->load(std::memory_order_acquire);

    while (true) {
        if (ptr == nullptr) {
            return false;
        }
        if (bits(ptr) & 1) {
            ptr2atomic =
                &(reinterpret_cast<std::atomic<void *> *>(filter_ptr(ptr))[traverser.advance()]);
            ptr = ptr2atomic->load(std::memory_order_acquire);
            continue;
        }
        ptr = Acquire(ptr2atomic);
        if (bits(ptr) & 1) {
            Release();
            ptr2atomic =
                &(reinterpret_cast<std::atomic<void *> *>(filter_ptr(ptr))[traverser.advance()]);
            ptr = ptr2atomic->load(std::memory_order_acquire);
            continue;
        } else if (ptr == nullptr) {
            Release();
            return false;
        } else {
            KV *kv = reinterpret_cast<KV *>(ptr);
            if (kv->key != key) {
                Release();
                return false;
            }
            bool cas_success =
                ptr2atomic->compare_exchange_strong(ptr, nullptr, std::memory_order_acq_rel);
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
        delete reinterpret_cast<Cell *>(filter_ptr(lhs));
    } else if (lhs != nullptr) {
        delete reinterpret_cast<KV *>(lhs.load());
    }
    if (bits(rhs) & 1) {
        delete reinterpret_cast<Cell *>(filter_ptr(rhs));
    } else if (rhs != nullptr) {
        delete reinterpret_cast<KV *>(rhs.load());
    }
}

template <class Key, class Value, class Hasher>
void HashTree<Key, Value, Hasher>::free_root(Root *ptr) {
    for (size_t i = 0; i < power(ptr->bit_count); ++i) {
        if (bits(ptr->ptrs[i]) & 1) {
            HashTree<Key, Value, Hasher>::Cell *cptr =
                reinterpret_cast<Cell *>(filter_ptr(ptr->ptrs[i]));
            delete cptr;
        } else if (ptr->ptrs[i] != nullptr) {
            KV *kv = reinterpret_cast<KV *>(ptr->ptrs[i].load());
            delete kv;
        }
    }
    free(ptr);
}

template <class Key, class Value, class Hasher>
HashTree<Key, Value, Hasher>::~HashTree() {
    for (Root *rptr : old_roots_) {
        if (rptr == nullptr) {
            continue;
        }
        for (size_t j = 0; j < power(rptr->bit_count); ++j) {
            Cell* cptr = reinterpret_cast<Cell*>(filter_ptr(rptr->ptrs[j]));
            cptr->lhs = nullptr;
            cptr->rhs = nullptr;
        }
        free_root(rptr);
    }
    free_root(root_.load());
}


template <class Key, class Value, class Hasher>
void HashTree<Key, Value, Hasher>::MergeRoot(int solidity) {
    Root* root;
    do {
        root = root_.load(std::memory_order_acquire);
    } while (root->bit_count != solidity - 2);

    size_t rs = power(root->bit_count);
    Root *new_root =
        reinterpret_cast<Root *>(malloc(sizeof(Root) + sizeof(std::atomic<void *>) * 2 * rs));
    new_root->bit_count = 1 + root->bit_count;

    for (size_t i = 0; i < rs; ++i) {
        Cell* cptr = reinterpret_cast<Cell *>(filter_ptr(root->ptrs[i]));
        void* lhs = cptr->lhs.load();
        assert(bits(lhs) & 1);
        new_root->ptrs[i] = lhs;
        void* rhs = cptr->rhs.load();
        assert(bits(rhs) & 1);
        new_root->ptrs[i + rs] = rhs;
    }
    root_.store(new_root, std::memory_order_release);
    old_roots_[solidity - 2] = root;
}
