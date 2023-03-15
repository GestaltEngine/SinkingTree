#include <cstdint>
#include <functional>

typedef uint64_t HashType;

namespace hashers {

HashType MurmurHash64A(uint64_t k) {
    const uint64_t m = 0xc6a4a7935bd1e995;
    const int r = 47;
    uint64_t h = 123 ^ (8 * m);
    k *= m;
    k ^= k >> r;
    k *= m;
    h ^= k;
    h *= m;
    h ^= h >> r;
    h *= m;
    h ^= h >> r;
    return h;
}

template <class Key, bool = std::is_integral<Key>::value>
struct DefaultHasher;

template <class Key>
struct DefaultHasher<Key, false> {
    HashType operator()(const Key &key) {
        return hasher_(key);
    }
    std::hash<Key> hasher_;
};

template <class Key>
struct DefaultHasher<Key, true> {
    HashType operator()(Key key) {
        // HashType x = key;
        // x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
        // x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
        // x = x ^ (x >> 31);
        return MurmurHash64A(key);
    }
};
}  // namespace hashers
