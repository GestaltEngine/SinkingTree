#include "hazard_ptr.h"
#include "hashers.h"

#include <atomic>
#include <cassert>
#include <cstdint>
#include <functional>
#include <optional>

namespace sinking_tree {

namespace {
constexpr HashType n_bit_mask(int n) {
    return (static_cast<HashType>(1) << n) - 1;
}

constexpr size_t power(int n) {
    return static_cast<size_t>(1) << n;
}

inline uintptr_t bits(void *ptr) {
    return reinterpret_cast<uintptr_t>(ptr);
}

inline uintptr_t filter_ptr(void *ptr) {
    return bits(ptr) & ~static_cast<uintptr_t>(1);
}
}  // namespace

using namespace hashers;

// declarations

enum class AcceptorState { kEmpty, kKeyValue, kCell };
enum class InjectorState { kEmpty, kKeyValue, kCell };

template <class Key, class Value, class Hasher = DefaultHasher<Key>>
class SinkingTree {
    struct Root {
        size_t bit_count;
        std::atomic<void *> ptrs[];
    };

    struct alignas(16) Cell {
        std::atomic<void *> lhs{};
        std::atomic<void *> rhs{};
        // the lowest bit is 0 - KV*
        // the lowest bit is 1 - Cell*
        ~Cell();
    };

    // alignment property ensures last pointer bit for this type is always zero,
    // even if K or V is an dataless type
    // making a set(V = void) with a single-bit key possible
    struct alignas(std::max(2UL, alignof(std::pair<Key, Value>))) KV {
        Key key;
        Value value;
    };

    class TreeTraverser {
    public:
        TreeTraverser(const Key &key, Hasher hasher)
            : key_ref_(key), hasher_(hasher), hash_(hasher_(key, 0)){};

        int Advance(int bit_count = 1) {
            while (bit_count > bits_alive_) {
                bits_consumed_ += bits_alive_;
                bit_count -= bits_alive_;
                hash_ = hasher_(key_ref_, bits_consumed_ / (sizeof(HashType) * 8));
                bits_alive_ = 8 * sizeof(HashType);
            }
            int index = hash_ & n_bit_mask(bit_count);
            hash_ >>= bit_count;
            bits_alive_ -= bit_count;
            bits_consumed_ += bit_count;
            return index;
        }

        int BitsConsumed() const {
            return bits_consumed_;
        }

    private:
        const Key &key_ref_;
        Hasher hasher_;
        HashType hash_;
        int bits_consumed_{0};
        uint8_t bits_alive_{8 * sizeof(HashType)};
    };

    static constexpr int kMaxSolidity_ = 8 * sizeof(HashType);

public:
    SinkingTree(size_t capacity = 2, Hasher hasher = Hasher());
    ~SinkingTree();

    bool Put(const Key &key, const Value &value);
    std::optional<Value> Get(const Key &key);
    bool Erase(const Key &key);

    SinkingTree(const SinkingTree &other) = delete;
    SinkingTree operator=(const SinkingTree &other) = delete;
    SinkingTree(SinkingTree &&other) = delete;
    SinkingTree operator=(SinkingTree &&other) = delete;

    void CleanupHazard();

private:
    void Sink(int);
    AcceptorState DeliberateState(void *);
    void FreeRoot(Root *);

    std::atomic<Root *> root_;
    Hasher hasher_;
    std::array<Root *, kMaxSolidity_> old_roots_{};
    std::atomic<size_t> cell_count_[kMaxSolidity_]{};

    Hazard<KV>::Manager manager_;
};

// definitions

template <class Key, class Value, class Hasher>
SinkingTree<Key, Value, Hasher>::SinkingTree(size_t capacity, Hasher hasher) : hasher_(hasher) {
    size_t bit_count = 1;
    size_t root_size = 1 << bit_count;
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
bool SinkingTree<Key, Value, Hasher>::Put(const Key &key, const Value &value) {
    auto mutator = manager_.MakeMutator();

    TreeTraverser traverser(key, hasher_);
    Root *root = root_.load(std::memory_order_acquire);
    std::atomic<void *> *ptr2atomic = &root->ptrs[traverser.Advance(root->bit_count)];

    void *desired = new KV{key, value};
    void *expected = ptr2atomic->load(std::memory_order_acquire);

    int migration_index = 0;

    void *second_extra = nullptr;
    InjectorState inj = InjectorState::kKeyValue;
    while (true) {
        do {
        deliberate:
            auto acc = DeliberateState(expected);
            if (inj == InjectorState::kCell) {
                Cell *discard = reinterpret_cast<Cell *>(filter_ptr(desired));
                reinterpret_cast<std::atomic<void *> *>(discard)[migration_index].store(
                    nullptr, std::memory_order_relaxed);
                delete discard;
                desired = second_extra;
                second_extra = nullptr;
                inj = InjectorState::kKeyValue;
            }
            if (acc == AcceptorState::kKeyValue) {
                void *ptr = mutator.Protect(0, *ptr2atomic);
                if (bits(ptr) & 1) {
                    ptr2atomic = &reinterpret_cast<std::atomic<void *> *>(
                        filter_ptr(ptr))[traverser.Advance()];
                    expected = ptr2atomic->load(std::memory_order_acquire);
                    goto deliberate;
                } else {
                    KV *acc_ptr = reinterpret_cast<KV *>(ptr);
                    KV *inj_ptr = reinterpret_cast<KV *>(desired);
                    if (acc_ptr->key == inj_ptr->key) {
                        expected = ptr;
                        continue;
                    }
                    // no Release() intended
                    Cell *new_cell = new Cell;
                    TreeTraverser repath(acc_ptr->key, hasher_);
                    repath.Advance(traverser.BitsConsumed());
                    migration_index = repath.Advance();
                    reinterpret_cast<std::atomic<void *> *>(new_cell)[migration_index].store(
                        ptr, std::memory_order_relaxed);

                    second_extra = desired;
                    desired = reinterpret_cast<void *>(bits(new_cell) | 1);
                    expected = ptr;
                    inj = InjectorState::kCell;
                }
            } else if (acc == AcceptorState::kCell) {
                ptr2atomic = &reinterpret_cast<std::atomic<void *> *>(
                    filter_ptr(expected))[traverser.Advance()];
                expected = ptr2atomic->load(std::memory_order_acquire);
                goto deliberate;
            } else {
                // inaction intended
            }
        } while (!ptr2atomic->compare_exchange_weak(expected, desired, std::memory_order_acq_rel));

        if (second_extra == nullptr) {
            break;
        } else {
            int solidity = traverser.BitsConsumed();
            if (solidity <= kMaxSolidity_) {
                auto before = cell_count_[solidity - 1].fetch_add(1);
                if (solidity > 1 && before + 1 == power(solidity) &&
                    traverser.BitsConsumed() - root->bit_count > 1) {
                    Sink(solidity);
                }
            }
            ptr2atomic = &
                reinterpret_cast<std::atomic<void *> *>(filter_ptr(desired))[traverser.Advance()];
            expected = nullptr;
            desired = second_extra;
            second_extra = nullptr;
            inj = InjectorState::kKeyValue;
        }
    }

    // cleanup the replaced KV if there is one
    if (expected != nullptr) {
        mutator.Retire(reinterpret_cast<KV *>(expected));
        return false;
    }
    return true;
}

template <class Key, class Value, class Hasher>
std::optional<Value> SinkingTree<Key, Value, Hasher>::Get(const Key &key) {
    auto mutator = manager_.MakeMutator();

    Root *root = root_.load(std::memory_order_acquire);
    TreeTraverser traverser(key, hasher_);
    std::atomic<void *> *ptr2atomic = &root->ptrs[traverser.Advance(root->bit_count)];
    void *ptr = ptr2atomic->load(std::memory_order_acquire);

    while (true) {
        if (ptr == nullptr) {
            return std::nullopt;
        }
        if (bits(ptr) & 1) {
            ptr2atomic =
                &reinterpret_cast<std::atomic<void *> *>(filter_ptr(ptr))[traverser.Advance()];
            ptr = ptr2atomic->load(std::memory_order_acquire);
            continue;
        }
        ptr = mutator.Protect(0, *ptr2atomic);
        if (ptr == nullptr) {
            return std::nullopt;
        } else if (bits(ptr) & 1) {
            ptr2atomic =
                &reinterpret_cast<std::atomic<void *> *>(filter_ptr(ptr))[traverser.Advance()];
            ptr = ptr2atomic->load(std::memory_order_acquire);
            continue;
        }
        KV *kv = reinterpret_cast<KV *>(ptr);
        std::optional<Value> ret_val;
        if (kv->key == key) {
            ret_val = kv->value;
        }
        return ret_val;
    }
}

template <class Key, class Value, class Hasher>
AcceptorState SinkingTree<Key, Value, Hasher>::DeliberateState(void *expected) {
    if (expected == nullptr) {
        return AcceptorState::kEmpty;
    } else if (bits(expected) & 1) {
        return AcceptorState::kCell;
    } else {
        return AcceptorState::kKeyValue;
    }
}

template <class Key, class Value, class Hasher>
bool SinkingTree<Key, Value, Hasher>::Erase(const Key &key) {
    auto mutator = manager_.MakeMutator();

    TreeTraverser traverser(key, hasher_);
    Root *root = root_.load(std::memory_order_acquire);
    std::atomic<void *> *ptr2atomic = &root->ptrs[traverser.Advance(root->bit_count)];
    void *ptr = ptr2atomic->load(std::memory_order_acquire);

    while (true) {
        if (ptr == nullptr) {
            return false;
        }
        if (bits(ptr) & 1) {
            ptr2atomic =
                &reinterpret_cast<std::atomic<void *> *>(filter_ptr(ptr))[traverser.Advance()];
            ptr = ptr2atomic->load(std::memory_order_acquire);
            continue;
        }
        ptr = mutator.Protect(0, *ptr2atomic);
        if (bits(ptr) & 1) {
            ptr2atomic =
                &reinterpret_cast<std::atomic<void *> *>(filter_ptr(ptr))[traverser.Advance()];
            ptr = ptr2atomic->load(std::memory_order_acquire);
            continue;
        } else if (ptr == nullptr) {
            return false;
        } else {
            KV *kv = reinterpret_cast<KV *>(ptr);
            if (kv->key != key) {
                return false;
            }
            bool cas_success =
                ptr2atomic->compare_exchange_strong(ptr, nullptr, std::memory_order_acq_rel);
            if (cas_success) {
                mutator.Retire(kv);
                return true;
            }
        }
    }
}

template <class Key, class Value, class Hasher>
SinkingTree<Key, Value, Hasher>::Cell::~Cell() {
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
void SinkingTree<Key, Value, Hasher>::FreeRoot(Root *ptr) {
    for (size_t i = 0; i < power(ptr->bit_count); ++i) {
        if (bits(ptr->ptrs[i]) & 1) {
            SinkingTree<Key, Value, Hasher>::Cell *cptr =
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
SinkingTree<Key, Value, Hasher>::~SinkingTree() {
    for (Root *rptr : old_roots_) {
        if (rptr == nullptr) {
            continue;
        }
        for (size_t j = 0; j < power(rptr->bit_count); ++j) {
            Cell *cptr = reinterpret_cast<Cell *>(filter_ptr(rptr->ptrs[j]));
            cptr->lhs = nullptr;
            cptr->rhs = nullptr;
        }
        FreeRoot(rptr);
    }
    FreeRoot(root_.load());
}

template <class Key, class Value, class Hasher>
void SinkingTree<Key, Value, Hasher>::Sink(int solidity) {
    Root *root;
    do {
        root = root_.load(std::memory_order_acquire);
    } while (root->bit_count != solidity - 2);

    size_t rs = power(root->bit_count);
    Root *new_root =
        reinterpret_cast<Root *>(malloc(sizeof(Root) + sizeof(std::atomic<void *>) * 2 * rs));
    new_root->bit_count = 1 + root->bit_count;

    for (size_t i = 0; i < rs; ++i) {
        Cell *cptr = reinterpret_cast<Cell *>(filter_ptr(root->ptrs[i]));
        void *lhs = cptr->lhs.load();
        assert(bits(lhs) & 1);
        new_root->ptrs[i] = lhs;
        void *rhs = cptr->rhs.load();
        assert(bits(rhs) & 1);
        new_root->ptrs[i + rs] = rhs;
    }
    root_.store(new_root, std::memory_order_release);
    old_roots_[solidity - 2] = root;
}

template <class Key, class Value, class Hasher>
void SinkingTree<Key, Value, Hasher>::CleanupHazard() {
    manager_.Cleanup();
}
}  // namespace sinking_tree
