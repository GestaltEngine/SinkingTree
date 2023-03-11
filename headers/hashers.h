#include <cstdint>
#include <functional>

typedef uint64_t HashType;

namespace hashers {
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
        HashType x = key;
        x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
        x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
        x = x ^ (x >> 31);
        return x;
    }
};
}  // namespace hashers
