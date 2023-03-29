# Structure

The data structure in question is a binary hash trie with the exception of its root which has a larger degree. It utilizes 3 types of nodes in its structure - a `Root`, which has a degree of $2^k$, where $k$ can grow over time; a `Cell` - a node which does not store keys and whose lifetime is tied to its ancestor `Root`; a `KV` - a leaf key-value pair. The structure utilizes marked pointer technique to differentiate between the `Cells` and `KVs` and make atomic operations on them possible without resorting to double-word CAS operations. In the present implementation pointers to `Cells` are marked with 1 in the lowest bit.

## Navigation

The position of the `KV` in the structure is determined by a suffix of the hash of the key. The lowest $k$ bits in the hash determine the index among the children in the root of size $2^k$ with the following bits determining which child - the left or the right one - of the `Cell` must be followed. The keys do not utilize the whole range of bits available to them, they are instead placed as deeply as necessary to avoid collisions on suffixes. Suppose a `KV` exists in the structure and its position is determined by some hash suffix, if a new, different key with the same hash-suffix is inserted, then both keys will be pushed down, until a point is reached where their suffixes differ by the highest bit. 

Since full collisions in hashes are possible, the structure uses seeded hashes to generate new hashes once bits of the previous ones are depleted. The implementation requires that for different keys their seeded hashes eventually differ as well.

## Insertion, Lookup and Deletion

The algorithm relies on atomic CAS operations to swap pointers as needed and on hazard pointers to safely delete nodes. See code for details.

## Sinking tree

If the algorithm encounters a situation where root has $2^k$ children and all of them are `Cells` and all their children exist and are `Cells` as well, it can perform a `Sink` operation that atomically replaces the `Root` (actually, a `Root*`) with the new one, twice as large as the previous, which contains pointers to the grandchildren of the previous. Since the lifetimes of the `Cells` are linked to the lifetimes of ancestor `Root`, the algorithm does not have to worry about them being changed by other processes while one thread collects them for the new `Root`. This effectively removes one layer of nodes from the equation and allows a faster access to keys as the structure grows.

## Performance

At every point in time the structure can be described using the set of hash-sequences of the keys in it. Any operation utilizing some new `key` will have to perform the amount of memory hops equal to the maximum length of collision of the hash-sequence of the `key` against suffixes of the hash-sequences in the structure. 

Suppose, we have `n` keys in the structure. Let us calculate the probability of a collision occuring that has length greater of equal to `k`. Let $coll(h_1, h_2)$ be the size of the largest common suffix for hash-sequences $h_1$ and $h_2$. Let $h$ be the hash-sequence of `key`.

$$
P(\exists i : coll(h, h_i)\geq k) = 1 - P(\forall i ~ coll(h, h_i) < k) = 1 - (P(coll(h, h_i) < k))^n = 
$$

$$
= 1 - (1 - P(coll(h, h_i) \geq k))^n = 1 - (1 - 2^{-k})^n
$$

$$ P(\max(coll(i, h_i)) = k) = (1 - 2^{-k - 1})^n - (1 - 2^{-k})^n \approx \exp(-\frac{n}{2^{k+1}}) - \exp(-\frac{n}{2^k})
$$

The graph of the function $f(k) = \exp(-\frac{n}{2^k+1}) - \exp(-\frac{n}{2^k})$ is almost zero almost everywhere, except the vicinity of its global maximum $k_0$.

$$
f'(k_0) = 0 \iff k_0 = \log(\frac{n}{2\ln(2)}) 
$$

The aforegoing calculation does not take the existance of `Root` in consideration. If there exists `Root` with size $2^r$, the first $r$ hops are skipped. Let us estimate $r$ now.

$r$ is such that all possible $r+3 = R$-long suffixes $s_R$ are present in our set of hash-sequences. This is a reverse coupons collector problem. Normally $\overline{n} = 2^R * R $, but for the reverse problem $\overline{R} \approx \log n - \log \log n $ (roughly). 

Which results in average $\log \log n$ time. 
