# 'Sinking Tree' hash-map

Almost* lock-free implementation of hash-map, free of limitations other lock-free hash maps impose.

## Advantages

- Allows rehashing without blocking lookups or insertions across the entire range of keys
- Allows any type of key, isn't limited to atomically CAS-able
- Good performance in concurrent scenarios
- Single-thread lookups are as fast as std::unordered_map
- Infinitely extensible 
- Erasures free memory, no tombstones
- Does not rely on any reserved invalid key value

## Limitations

- Single-threaded insertion time is worse than std::unordered_map 
- As the actual size of the map grows beyond the expected capacity, insertion, lookup and erase time complexity degrades to `O(log log n)`
- Currently, there are opportunities for the map to be less memory-hungry if lock-free atomic shared pointers are implemented, albeit it's still ok without them
- Relies on hazard pointers for safe key deletion - latency is bad in the worst case
- *Rehashing requires locking and has the same latency problem (this could be solved with each Put doing a smaller part of the rehashing)
- Extremely reliant on a good hash-function
- Requires a seeded hash-function in order to be reliable in worst-case scenarios
- Currently does not have iterators
- Does not shrink.

## How does it work and why is it named like that

See `paper.md` for details.
