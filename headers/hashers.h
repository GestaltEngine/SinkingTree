#include <cstdint>
#include <functional>

/*
    Murmurhash is covered by MIT license.
*/

typedef uint64_t HashType;

namespace hashers {

HashType MurmurHash64A(uint64_t k, uint64_t seed) {
    const uint64_t m = 0xc6a4a7935bd1e995;
    const int r = 47;
    uint64_t h = seed ^ (8 * m);
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

HashType MurmurHash64A(const void *key, int len, uint64_t seed) {
    const uint64_t m = 0xc6a4a7935bd1e995;
    const int r = 47;
    uint64_t h = seed ^ (len * m);
    const uint64_t *data = (const uint64_t *)key;
    const uint64_t *end = data + (len / 8);
    while (data != end) {
        uint64_t k = *data++;
        k *= m;
        k ^= k >> r;
        k *= m;
        h ^= k;
        h *= m;
    }
    const unsigned char *data_remain = reinterpret_cast<const unsigned char *>(data);
    switch (len & 7) {
        case 7:
            h ^= ((uint64_t)data_remain[6]) << 48;
        case 6:
            h ^= ((uint64_t)data_remain[5]) << 40;
        case 5:
            h ^= ((uint64_t)data_remain[4]) << 32;
        case 4:
            h ^= ((uint64_t)data_remain[3]) << 24;
        case 3:
            h ^= ((uint64_t)data_remain[2]) << 16;
        case 2:
            h ^= ((uint64_t)data_remain[1]) << 8;
        case 1:
            h ^= ((uint64_t)data_remain[0]);
            h *= m;
    };
    h ^= h >> r;
    h *= m;
    h ^= h >> r;
    return h;
}

template <class Key, bool = std::is_integral<Key>::value>
struct DefaultHasher;

template <class Key>
struct DefaultHasher<Key, false> {
    HashType operator()(const Key &key, uint64_t seed) {
        return MurmurHash64A(key.data(), key.size() * sizeof(Key), seed);
    }
};

template <class Key>
struct DefaultHasher<Key, true> {
    HashType operator()(Key key, uint64_t seed) {
        return MurmurHash64A(key, seed);
    }
};
}  // namespace hashers
