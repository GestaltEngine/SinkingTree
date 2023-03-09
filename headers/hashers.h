#include <cstdint>
#include <functional>

namespace hashers {
template <class Key>
struct DefaultHasher {
    uint32_t operator()(const Key &key) {
        return hasher_(key);
    }
    std::hash<Key> hasher_;
};

template <>
struct DefaultHasher<uint32_t> {
    uint32_t operator()(uint32_t x) {
        x = ((x >> 16) ^ x) * 0x45d9f3b;
        x = ((x >> 16) ^ x) * 0x45d9f3b;
        x = (x >> 16) ^ x;
        return x;
    }
};

template <>
struct DefaultHasher<int> {
    uint32_t operator()(uint32_t x) {
        x = ((x >> 16) ^ x) * 0x45d9f3b;
        x = ((x >> 16) ^ x) * 0x45d9f3b;
        x = (x >> 16) ^ x;
        return x;
    }
};
}  // namespace hashers
