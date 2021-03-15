// Copyright: ThoughtSpot Inc. 2021
// Author: Mohit Saini (mohit.saini@thoughtspot.com)

// The documentation (ts/lazy_map.md) MUST be read twice
// before using this utility.

#ifndef TS_LAZY_MAP_HPP_
#define TS_LAZY_MAP_HPP_

#include <assert.h>

#include <memory>
#include <utility>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <string>

namespace ts {
namespace lazy_map_impl {

constexpr const char* key_error = "[lazy_map]: Key not found";

template<typename C, typename K>
inline bool contains_key(const C& c, const K& k) {
  return (c.find(k) != c.end());
}

template<typename K, typename V>
class lazy_map {
  class const_iter_impl;
  using underlying_map = typename std::unordered_map<K, V>;
  using underlying_const_iter = typename underlying_map::const_iterator;

 public:
  using key_type = K;
  using mapped_type = V;
  using value_type = std::pair<const K, V>;
  using const_iterator = const_iter_impl;
  using iterator = const_iterator;
  using reference = value_type&;
  using const_reference = const value_type&;
  lazy_map() : node_(std::make_shared<Node>()) { }
  lazy_map(std::initializer_list<value_type> values)
    : node_(std::make_shared<Node>(values)) { }
  template<typename InputIt>
  lazy_map(InputIt first, InputIt last)
    : node_(std::make_shared<Node>(first, last)) { }

  bool detach() {
    prepare_for_edit();
    return detach_internal();
  }

  bool is_detached() const {
    return (node_->parent_ == nullptr);
  }

  bool contains(const K& k) const {
    return contains_internal(k);
  }

  size_t get_depth() const {
    size_t depth = 0;
    for (auto* p = &node_->parent_; (*p) != nullptr; p = &((*p)->parent_)) {
      depth++;
    }
    return depth;
  }

  const V& at(const K& k) const {
    for (auto* p = &node_; (*p) != nullptr; p = &((*p)->parent_)) {
      if (contains_key((*p)->values_, k)) {
        return (*p)->values_.at(k);
      }
      if (contains_key((*p)->deleted_keys_, k)) {
        throw std::runtime_error(key_error);
      }
    }
    throw std::runtime_error(key_error);
  }

  const V& operator[](const K& k) const {
    return at(k);
  }

  size_t size() const {
    return node_->size_;
  }

  bool empty() const {
    return (node_->size_ == 0);
  }

  void insert_or_assign(const K& k, const V& v) {
    prepare_for_edit();
    node_->size_ += contains_internal(k) ? 0: 1;
    node_->deleted_keys_.erase(k);
    if (contains_key(node_->values_, k)) {
      node_->values_.at(k) = v;
    } else {
      node_->values_.emplace(k, v);
    }
  }

  void insert_or_assign(const K& k, V&& v) {
    prepare_for_edit();
    node_->size_ += contains_internal(k) ? 0: 1;
    node_->deleted_keys_.erase(k);
    if (contains_key(node_->values_, k)) {
      node_->values_.at(k) = std::move(v);
    } else {
      node_->values_.emplace(k, std::move(v));
    }
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
    node_->deleted_keys_.erase(k);
    node_->values_.emplace(k, v);
    node_->size_++;
    return true;
  }

  bool insert(const K& k, V&& v) {
    if (contains_internal(k)) return false;
    prepare_for_edit();
    node_->deleted_keys_.erase(k);
    node_->values_.emplace(k, std::move(v));
    node_->size_++;
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
    node_->deleted_keys_.erase(k);
    node_->values_.emplace(std::piecewise_construct,
                           std::forward_as_tuple(k),
                           std::tuple<Args&&...>(std::forward<Args>(args)...));
    node_->size_++;
    return true;
  }

  void clear() {
    // No need to prepare_for_edit.
    node_ = std::make_shared<Node>();
  }

  bool erase(const K& k) {
    if (not contains_internal(k)) return false;
    prepare_for_edit();
    node_->values_.erase(k);
    if (contains_internal(k)) {
      node_->deleted_keys_.insert(k);
    }
    node_->size_--;
    return true;
  }

  // - Erase the given key and return the erased value. If the key-value pair
  //   is not shared by other objects, move the value in output. If the
  //   key-value pair is shared by other objects, copy the value for returning.
  // - It is useful when we need to update a value efficiently.
  // - Returns nullptr only if the key doesn't exists.
  // - Non-standard map method.
  std::unique_ptr<V> move_and_erase(const K& k) {
    if (not contains_internal(k)) return nullptr;
    prepare_for_edit();
    std::unique_ptr<V> output;
    if (contains_key(node_->values_, k)) {
      output = std::make_unique<V>(std::move(node_->values_[k]));
    } else {
      output = std::make_unique<V>(at(k));
    }
    node_->values_.erase(k);
    if (contains_internal(k)) {
      node_->deleted_keys_.insert(k);
    }
    node_->size_--;
    return output;
  }

  // - Move out the value of a key and return. Return nullptr if the key
  //   doesn't exists. If the value is shared by other objects, it will be
  //   copied.
  // - It is useful when we need to update the value efficiently.
  // - Equivalent of std::make_unique<V>(std::move(my_map[key])) in standard
  //   map.
  // - Since lazy_map don't expose mutable iternal reference,
  //   only way to update a value of key is: copy/move it in a variable, update
  //   it and then insert_or_assign it again.
  // - Return nullptr if either the key doesn't exists.
  // - Non-standard map method.
  std::unique_ptr<V> move(const K& k) {
    if (not contains_internal(k)) return nullptr;
    if (node_.unique() and contains_key(node_->values_, k)) {
      return std::make_unique<V>(std::move(node_->values_[k]));
    } else {
      return std::make_unique<V>(at(k));
    }
  }

  // Similar to move(k) method. In adding, it return the nullptr if the value
  // is shared instead of copying value.
  std::unique_ptr<V> move_only(const K& k) {
    if (not contains_internal(k)) return nullptr;
    if (node_.unique() and contains_key(node_->values_, k)) {
      return std::make_unique<V>(std::move(node_->values_[k]));
    }
    return nullptr;
  }

  const_iter_impl begin() const {
    return const_iter_impl(node_.get());
  }

  const_iter_impl end() const {
    return const_iter_impl(nullptr);
  }

  const_iterator find(const K& k) const {
    for (Node* p = node_.get(); p != nullptr; p = p->parent_.get()) {
      auto it = p->values_.find(k);
      if (it != p->values_.end()) {
        return const_iterator(node_.get(), p, std::move(it));
      }
      if (contains_key(p->deleted_keys_, k)) {
        return const_iterator();
      }
    }
    return const_iterator();
  }

 private:
  bool insert_internal(const K& k, const V& v) {
    if (contains_internal(k)) return false;
    node_->deleted_keys_.erase(k);
    node_->values_.emplace(k, v);
    node_->size_++;
    return true;
  }

  bool contains_internal(const K& k) const {
    for (Node* p = node_.get(); p != nullptr; p = p->parent_.get()) {
      if (contains_key(p->values_, k)) {
        return true;
      }
      if (contains_key(p->deleted_keys_, k)) {
        return false;
      }
    }
    return false;
  }

  void prepare_for_edit() {
    if (not node_.unique()) {
      auto new_node = std::make_shared<Node>(std::move(node_));
      node_ = std::move(new_node);
    }
  }

  bool detach_internal() {
    if (node_->parent_ == nullptr) return false;
    for (auto* p = &node_->parent_; (*p) != nullptr; p = &((*p)->parent_)) {
      for (auto& v : (*p)->values_) {
        if (not contains_key(node_->deleted_keys_, v.first)) {
          node_->values_.emplace(v.first, v.second);
        }
      }
      const auto& d = (*p)->deleted_keys_;
      node_->deleted_keys_.insert(d.begin(), d.end());
    }
    node_->deleted_keys_.clear();
    node_->parent_ = nullptr;
    return true;
  }

  struct Node {
    Node() = default;
    explicit Node(std::shared_ptr<Node>&& parent)
      : parent_(std::move(parent)), size_(parent_->size_) { }
    explicit Node(std::initializer_list<value_type> values)
      : values_(values), size_(values_.size()) { }
    explicit Node(const std::unordered_map<K, V>& other_map)
      : values_(other_map), size_(values_.size()) { }
    explicit Node(std::unordered_map<K, V>&& other_map)
      : values_(std::move(other_map)), size_(values_.size()) { }
    template<typename InputIt>
    Node(InputIt first, InputIt last)
      : values_(first, last), size_(values_.size()) { }
    std::shared_ptr<Node> parent_;
    std::unordered_map<K, V> values_;
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
    const_iter_impl(const Node* head,
                    const Node* current,
                    underlying_const_iter&& it)
      : head_(head), current_(current), it_(std::move(it)) {}

    const_iter_impl(const Node* head): head_(head), current_(head) {
      if (current_) {
        it_ = current_->values_.begin();
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
      while (it_ == current_->values_.end()) {
        if (current_->parent_ == nullptr) {
          return false;
        }
        current_ = current_->parent_.get();
        it_ = current_->values_.begin();
      }
      return true;
    }
    // Precondition(@current_ != nullptr)
    bool should_ignore_key(const K& k) const {
      for (auto c = head_; c != current_; c = c->parent_.get()) {
        if (contains_key(c->values_, k)
             or contains_key(c->deleted_keys_, k)) {
          return true;
        }
      }
      return false;
    }
    // Invariant(head_ != nullptr || current_ == nullptr)
    const Node* head_ = nullptr;
    // current_ == nullptr means that this iterator is the `end()`
    const Node* current_ = nullptr;
    // Belongs to the containr current_->values_ if current_ is not nullptr.
    underlying_const_iter it_;
    friend class lazy_map;
  };
  friend class lazy_map_test_internals;
  std::shared_ptr<Node> node_;
};

}  // namespace lazy_map_impl

using lazy_map_impl::lazy_map;

}  // namespace ts

#endif  // TS_LAZY_MAP_HPP_
