// Author: Mohit Saini (mohitsaini1196@gmail.com)

// The documentation (lazy_map.md) MUST be read twice
// before using this utility.

#ifndef QUICK_LAZY_MAP_HPP_
#define QUICK_LAZY_MAP_HPP_

#include <cassert>
#include <memory>
#include <utility>
#include <unordered_map>
#include <unordered_set>

namespace quick {
namespace lazy_map_impl {

constexpr const char* key_error = "[lazy_map]: Key not found";

template<typename C, typename K>
bool contains_key(const C& c, const K& k) {
  return (c.find(k) != c.end());
}

// Given a mutable map and it's const_iterator, return the mutable iterator
// corresponding to the given const_iterator.
// Note: here 'erase' is not doing anything because size of range = 0, since
// begin == end here. We are abusing 'erase' method to return non-const iter.
template<typename M>
typename M::iterator to_non_const_iter(
    M& m, typename M::const_iterator const_it) {
  return m.erase(const_it, const_it);
}

// Does : `container[k] = v`  in a better way.
template<typename C, typename K, typename V>
void put_key_value(C& container, K&& k, V&& v) {
  auto&& it = container.find(k);
  if (it == container.end()) {
    container.emplace(std::forward<K>(k), std::forward<V>(v));
  } else {
    it->second = std::forward<V>(v);
  }
}

template<typename K, typename V>
class lazy_map {
  class const_iter_impl;
  using underlying_map = typename std::unordered_map<K, V>;
  using underlying_const_iter = typename underlying_map::const_iterator;
  struct Fragment;

 public:
  using key_type = K;
  using mapped_type = V;
  using value_type = typename underlying_map::value_type;
  using const_iterator = const_iter_impl;
  using iterator = const_iterator;
  using reference = value_type&;
  using const_reference = const value_type&;
  lazy_map() : head_(std::make_shared<Fragment>()) { }
  lazy_map(std::initializer_list<value_type> values)
    : head_(std::make_shared<Fragment>(values)) { }
  template<typename InputIt>
  lazy_map(InputIt first, InputIt last)
    : head_(std::make_shared<Fragment>(first, last)) { }

  bool detach() {
    prepare_for_edit();
    return detach_internal();
  }

  bool is_detached() const {
    return (head_->parent() == nullptr);
  }

  bool contains(const K& k) const {
    return contains_internal(k);
  }

  size_t get_depth() const {
    size_t depth = 0;
    for (const Fragment* p = head_->parent(); p != nullptr; p = p->parent()) {
      depth++;
    }
    return depth;
  }

  const V& at(const K& k) const {
    auto&& it = find(k);
    if (it.is_end()) {
      throw std::out_of_range(key_error);
    } else {
      return it->second;
    }
  }

  const V& operator[](const K& k) const {
    return at(k);
  }

  size_t size() const {
    return head_->size_;
  }

  bool empty() const {
    return size() == 0;
  }

  void insert_or_assign(const K& k, const V& v) {
    prepare_for_edit();
    head_->size_ += contains_internal(k) ? 0: 1;
    head_->deleted_keys_.erase(k);
    put_key_value(head_->key_values_, k, v);
  }

  void insert_or_assign(const K& k, V&& v) {
    prepare_for_edit();
    head_->size_ += contains_internal(k) ? 0: 1;
    head_->deleted_keys_.erase(k);
    put_key_value(head_->key_values_, k, std::move(v));
  }

  void insert_or_assign(const value_type& kv) {
    insert_or_assign(kv.first, kv.second);
  }

  void insert_or_assign(value_type&& kv) {
    insert_or_assign(kv.first, std::move(kv.second));
  }

  // Non standard method. insert_or_assign is the standard name for it (C++17).
  inline void put(const K& k, const V& v) {
    insert_or_assign(k, v);
  }

  // Non standard method. insert_or_assign is the standard name for it (C++17).
  inline void put(const K& k, V&& v) {
    insert_or_assign(k, std::move(v));
  }

  bool insert(const K& k, const V& v) {
    if (contains_internal(k)) return false;
    prepare_for_edit();
    head_->deleted_keys_.erase(k);
    head_->key_values_.emplace(k, v);
    head_->size_++;
    return true;
  }

  bool insert(const K& k, V&& v) {
    if (contains_internal(k)) return false;
    prepare_for_edit();
    head_->deleted_keys_.erase(k);
    head_->key_values_.emplace(k, std::move(v));
    head_->size_++;
    return true;
  }

  bool insert(const value_type& kv) {
    return insert(kv.first, kv.second);
  }

  bool insert(value_type&& kv) {
    return insert(kv.first, std::move(kv.second));
  }

  template<typename... Args>
  bool emplace(const K& k, Args&&... args) {
    if (contains_internal(k)) return false;
    prepare_for_edit();
    head_->deleted_keys_.erase(k);
    head_->key_values_.emplace(std::piecewise_construct,
                           std::forward_as_tuple(k),
                           std::tuple<Args&&...>(std::forward<Args>(args)...));
    head_->size_++;
    return true;
  }

  void clear() {
    // No need to prepare_for_edit.
    head_ = std::make_shared<Fragment>();
  }

  bool erase(const K& k) {
    if (not contains_internal(k)) return false;
    prepare_for_edit();
    head_->key_values_.erase(k);
    if (contains_internal(k)) {
      head_->deleted_keys_.insert(k);
    }
    head_->size_--;
    return true;
  }

  // - Move out the value of a key and return. Raise exception if the key
  //   doesn't exists. If the value is shared by other objects, it will be
  //   copied.
  // - It is useful when we need to update the value efficiently.
  // - Equivalent of std::move(my_map.at(key)) in standard map.
  // - Since lazy_map don't expose mutable iternal reference,
  //   only way to update a value of key is: copy/move it in a variable, update
  //   it and then insert_or_assign it again.
  // - This is a non-standard map method.
  V move(const K& k) {
    auto&& iter = find(k);
    if (iter.is_end()) {
      throw std::out_of_range(key_error);
    } else {
      return move(iter);
    }
  }

  // - Move the value at @iter from this map and return. After this operation,
  //   iter->second will be in moved-from state.
  // - If the value is shared by other objects, it will be copied.
  // - If you cannot afford to copy, use move_only method.
  // - Behavior is undefined if @iter is past the end.
  // - This is a non-standard map method.
  V move(const const_iter_impl& iter) {
    if (head_.unique() and iter.current_ == head_.get()) {
      return std::move(to_non_const_iter(head_->key_values_, iter.it_)->second);
    } else {
      return iter.it_->second;
    }
  }

  // Similar to move(k) method but the difference is:
  //  it return empty optional if the value is shared by other objects.
  std::optional<V> move_only(const K& k) {
    auto&& iter = find(k);
    if (iter.is_end()) {
      throw std::out_of_range(key_error);
    } else {
      return move_only(iter);
    }
  }

  // - Move the value at @iter from this map and return. After this operation,
  //   iter->second will be in moved-from state.
  // - If the value is shared by other objects, empty optional will be returned.
  // - If you cannot afford empty std::optional, use 'move' method above.
  // - Behavior is undefined if @iter is past the end.
  std::optional<V> move_only(const const_iter_impl& iter) {
    if (head_.unique() and iter.current_ == head_.get()) {
      return std::move(to_non_const_iter(head_->key_values_, iter.it_)->second);
    } else {
      return std::optional<V>();
    }
  }

  const_iter_impl begin() const {
    return const_iter_impl(head_.get());
  }

  const_iter_impl end() const {
    return const_iter_impl(nullptr);
  }

  const_iterator find(const K& k) const {
    for (const Fragment* p = head_.get(); p != nullptr; p = p->parent()) {
      auto it = p->key_values_.find(k);
      if (it != p->key_values_.end()) {
        return const_iter_impl(head_.get(), p, std::move(it));
      }
      if (contains_key(p->deleted_keys_, k)) {
        return const_iter_impl(nullptr);
      }
    }
    return const_iter_impl(nullptr);
  }

 private:
  bool insert_internal(const K& k, const V& v) {
    if (contains_internal(k)) return false;
    head_->deleted_keys_.erase(k);
    head_->key_values_.emplace(k, v);
    head_->size_++;
    return true;
  }

  static bool contains_internal(const Fragment* node, const K& k) {
    for (const Fragment* p = node; p != nullptr; p = p->parent()) {
      if (contains_key(p->key_values_, k)) {
        return true;
      }
      if (contains_key(p->deleted_keys_, k)) {
        return false;
      }
    }
    return false;
  }

  bool contains_internal(const K& k) const {
    return contains_internal(head_.get(), k);
  }

  void prepare_for_edit() {
    if (not head_.unique()) {
      auto new_node = std::make_shared<Fragment>(std::move(head_));
      head_ = std::move(new_node);
    }
  }

  bool detach_internal() {
    if (head_->parent_ == nullptr) return false;
    for (const Fragment* p = head_->parent(); p != nullptr; p = p->parent()) {
      for (auto& v : p->key_values_) {
        if (not contains_key(head_->deleted_keys_, v.first)) {
          head_->key_values_.emplace(v.first, v.second);
        }
      }
      const auto& d = p->deleted_keys_;
      head_->deleted_keys_.insert(d.begin(), d.end());
    }
    head_->deleted_keys_.clear();
    head_->parent_ = nullptr;
    return true;
  }

  struct Fragment {
    Fragment() = default;
    explicit Fragment(std::shared_ptr<Fragment>&& parent)
      : parent_(std::move(parent)), size_(parent_->size_) { }
    explicit Fragment(std::initializer_list<value_type> values)
      : key_values_(values), size_(key_values_.size()) { }
    explicit Fragment(const std::unordered_map<K, V>& other_map)
      : key_values_(other_map), size_(key_values_.size()) { }
    explicit Fragment(std::unordered_map<K, V>&& other_map)
      : key_values_(std::move(other_map)), size_(key_values_.size()) { }
    template<typename InputIt>
    Fragment(InputIt first, InputIt last)
      : key_values_(first, last), size_(key_values_.size()) { }
    // Returns const parent. UB if parent is nullptr.
    const Fragment* parent() const { return parent_.get(); };
    Fragment* mutable_parent() { return parent_.get(); };
    std::shared_ptr<Fragment> parent_;
    std::unordered_map<K, V> key_values_;
    std::unordered_set<K> deleted_keys_;
    size_t size_ = 0;
  };
  // The implementation of this iterator relies on the C++ standard's sayings,
  // that comparison of two iterators from different container is undefined
  // behavior. Hence iterator of a lazy_map object should not
  // be compared with other lazy_map object.
  // In this implementation we are also ensuring that we don't compare iterator
  // of one unordered_map with another.
  class const_iter_impl {
   public:
    // Default constructed iterator is the end() iterator.
    const_iter_impl() = default;
    const_iter_impl(const Fragment* head,
                    const Fragment* current,
                    underlying_const_iter&& it)
      : head_(head), current_(current), it_(std::move(it)) {}

    const_iter_impl(const Fragment* head): head_(head), current_(head) {
      if (current_) {
        it_ = current_->key_values_.begin();
        if (not move_forward_to_closest_non_deleted_valid_position()) {
          current_ = nullptr;
        }
      }
    }
    bool operator==(const const_iter_impl& o) const {
      return (current_ == o.current_ && (current_ == nullptr || it_ == o.it_));
    }
    bool operator!=(const const_iter_impl& o) const {
      return not (*this == o);
    }
    // Precondition(@current_ != nullptr)
    const_iter_impl& operator++() {
      assert(current_ != nullptr);
      ++it_;
      if (not move_forward_to_closest_non_deleted_valid_position()) {
        current_ = nullptr;
        return *this;
      }
      return *this;
    }
    const_iter_impl& operator++(int) {
      auto old = *this;
      ++(*this);
      return old;
    }
    auto& operator*() const {
      return *it_;
    }
    auto* operator->() const {
      return it_.operator->();
    }
    bool is_end() const { return current_ == nullptr; }

   private:
    // - Precondition(@current_ != nullptr)
    // - Postcondition(@current_ != nullptr)
    // - The closest non-deleted valid position might be at 0 distance apart.
    //    (if we are already there).
    // - Return false if reached end of stream.
    bool move_forward_to_closest_non_deleted_valid_position() {
      while(move_forward_to_closest_valid_position()) {
        if (should_ignore_key(it_->first)) {
          ++it_;
          continue;
        } else {
          return true;
        }
      }
      return false;
    }
    // - Precondition(@current_ != nullptr)
    // - Postcondition(@current_ != nullptr)
    // - The closest valid position might be at 0 distance apart. (if we are
    //   already on a valid position).
    // - Return false if we failed to move forward to a valid position.
    bool move_forward_to_closest_valid_position() {
      while (it_ == current_->key_values_.end()) {
        if (current_->parent_ == nullptr) {
          return false;
        }
        current_ = current_->parent_.get();
        it_ = current_->key_values_.begin();
      }
      return true;
    }
    // Precondition(@current_ != nullptr)
    bool should_ignore_key(const K& k) const {
      for (auto c = head_; c != current_; c = c->parent_.get()) {
        if (contains_key(c->key_values_, k)
             or contains_key(c->deleted_keys_, k)) {
          return true;
        }
      }
      return false;
    }
    // Invariant(head_ != nullptr || current_ == nullptr)
    const Fragment* head_ = nullptr;
    // current_ == nullptr means that this iterator is the `end()`
    const Fragment* current_ = nullptr;
    // `it_` is a iterator of `current_->key_values_` container if @current_
    // is not nullptr. Default constructed o.w.
    underlying_const_iter it_;
    friend class lazy_map;
  };
  friend class lazy_map_test_internals;

 private:
  std::shared_ptr<Fragment> head_;
};

}  // namespace lazy_map_impl

using lazy_map_impl::lazy_map;

}  // namespace quick

#endif  // QUICK_LAZY_MAP_HPP_
