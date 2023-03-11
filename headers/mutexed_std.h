#include <mutex>
#include <optional>
#include <unordered_map>

template <class Key, class Value>
class Baseline {
public:
    Baseline(size_t capacity) : map_(capacity) {
    }

    bool Put(Key key, Value value) {
        std::lock_guard lock(mutex_);
        auto res = map_.insert({std::move(key), std::move(value)});
        return res.second;
    }
    std::optional<Value> Get(const Key& key) {
        std::lock_guard lock(mutex_);
        auto res = map_.find(key);
        if (res == map_.end()) {
            return std::nullopt;
        }
        return res->second;
    }
    bool Erase(const Key& key) {
        std::lock_guard lock(mutex_);
        return 1 == map_.erase(key);
    }
    size_t Cap() const {
        std::lock_guard lock(mutex_);
        return map_.bucket_count();
    }

private:
    std::unordered_map<Key, Value> map_;
    mutable std::mutex mutex_;
};
