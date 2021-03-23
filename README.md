## lazy_map

`lazy_map` is an implementation of `std::unordered_map` that has `O(1)` 
cost of copying irrespective of the current size of map.
The optimizations done for `O(1)` copy operation don't have any
visible side-effects on the map interface no matter how `lazy_map`
is used. The value semantics of the map are preserved while copying, i.e.,
any write operation on the copied map are totally independent of copied-from
map. For any two different `lazy_map` objects, write operation on one has no
impact on the other one.
The map operations like insertion, deletion, lookup etc. continue
to cost `O(1)` as usual, except in the special case of detachment.

---

### Client of lazy_map Must Know That

1. The iterator can get invalidated on any standard write operation (unlike
   `std::unordered_map`, which guarantees iterator stability on erase).
   `lazy_map` offers two non standard methods `move_value` and
   `move` to move out the value of a key. `lazy_map` guarantees iterator stability
   on `move_value` and `move` operation.

2. If the cost of copy for value-type is large, it's good idea to wrap the
   value type in a `cow_wrapper` (e.g.: `lazy_map<int, cow_wrapper<V>>`)
   because the internal implementation of `lazy_map` might have to copy
   the value multiple times. The time complexity analysis here assumes
   that cost of copying key/value is `O(1)`.

3. Standard map methods, which expose mutable internal references, are *NOT*
   supported. eg: non-const operator[], non-const iterator etc.

4. In addition to the standard `find` method, `contains` method is
   supported that takes a key and return a boolean.

5. Non standard methods `move_value` and `move` take a `key` and
   return a `unique_ptr<Value>` by moving the value at `key`.
   `move_value` returns `nullptr` if the value is shared by other objects.
   `move` copies the value if the value is shared by other
   objects. Both of these method return `nullptr` if the key doesn't exist.
   ToDo(Mohit): Revisit this point.

---

### Implementation Overview:

The implementation of `lazy_map` stores the data of a map in a
chain of fragments. Each fragment stores the modification to be applied
on the map state. These fragments are shared across different map objects. If a
map is copied, the new object becomes another share holder of the old
fragment (think `std::shared_ptr`). The map updates (insertion, deletion) on
the copied object are stored on a new fragment on top of the current
fragment. Fragments can have a parent fragment. These fragment create a tree
like structure (similar to git commits). The absolute value of a fragment is
computed by applying the modification in current fragment on the absolute
value of parent fragment.

If a fragment has a long chain of parents (i.e. length of chain > `@max_depth`),
the absolute value of a fragment is computed and updated inplace and the
fragment is detached from its parent. This operation is called detachment.
It is the most expensive operation in `lazy_map`.
Note: default value of `@max_depth` is 3.

The iteration on `lazy_map` costs `O(number_of_keys * depth_of_parents_chain)`. In
fact for most of the `lazy_map` APIs, a factor of `@depth_of_parents_chain` comes
into time complexity. Which is not a big deal if we use a small `@max_depth`.
Note that `lazy_map` is not optimized for deeply nested fragments, but it is
highly optimized for fragment trees with very high breadth/branches,
i.e., thousands of copies of a very big map for making small modifications can save
cost of copying with `lazy_map`. 

---

### Implementation Details:

Shared fragments in `lazy_map` are both value-immutable as well as
structure-immutable. 

A fragment can be detached only if it has exactly one parent. Hence `lazy_map`
doesn't need to protect its fragments by a read-write lock.

Note that `lazy_map` doesn't track the depth of parents chain. They
need to be detached manually when required. Generally it's good idea to
detach them manually if the fragment chain is going to be very large.

#### Fragment:

```C++
struct Fragment {
  std::shared_ptr<Fragment> parent;
  std::unordered_map<K, V> key_values;
  std::unordered_set<K> deleted_keys;
};
```

A fragment records the deleted keys as well as updated key-value pairs.

The absolute value of a fragment is computed as follows:
```C++
AbsoluteValue(fragment) {
  if (fragment.parent == nullptr) return fragment.key_values;
  else {
    let m = AbsoluteValue(*fragment.parent);
    m.erase_keys(fragment.deleted_keys);
    m.override_key_values(fragment.key_values);
    return m;
  }
}
```

Following invariants are guaranteed in every fragment.

1. `fragment.key_values` and `fragment.deleted_keys` should be disjoint.

2. if `x ∈ fragment.deleted_keys` then
     (`fragment.parent`  must not be `nullptr`
      &&  `x ∈ AbsoluteValue(fragment.parent)`)


Example:

F1 = (key_values={1: 10, 2: 20, 3:30}, deleted_keys={}, parent=nullptr)

F2 = (key_values={2: 30, 4: 40}, deleted_keys={1}, parent=F1)

F3 = (key_values={1: 10, 7: 70, 6:60}, deleted_keys={2, 4}, parent=F2)

AbsoluteValue(F1) = {1: 10, 2: 20, 3:30}

AbsoluteValue(F2) = {2: 30, 3:30, 4: 40}

AbsoluteValue(F3) = {1: 10, 3: 30, 6: 60, 7:70}


Detached(F3):
= (key_values={1: 10, 3: 30, 6: 60, 7:70}, deleted_keys={}, parent=nullptr)


AbsoluteValue doesn't change by detachment.

