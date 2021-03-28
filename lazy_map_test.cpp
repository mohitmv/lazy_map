// Copyright: ThoughtSpot Inc. 2021
// Author: Mohit Saini (mohit.saini@thoughtspot.com)

#include "lazy_map.hpp"

#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "gtest/gtest.h"

using std::vector;
using quick::lazy_map;

struct CopyMoveCounter {
  using This = CopyMoveCounter;
  struct Info {
    int copy_ctr = 0;
    int copy_assign = 0;
    int move_ctr = 0;
    int move_assign = 0;
    int value_ctr = 0;
    int moves() const { return move_ctr + move_assign; }
    int copies() const { return copy_ctr + copy_assign; }
    int total() const { return moves() + copies() + value_ctr; }
    void reset() { *this = Info(); }
  };
  CopyMoveCounter(Info* info): info_(info) { ++(info_->value_ctr); }
  CopyMoveCounter(const This& o): info_(o.info_) { ++(info_->copy_ctr); }
  CopyMoveCounter(This&& o) noexcept : info_(o.info_) { ++(info_->move_ctr); }
  This& operator=(const This& o) {
    info_ = o.info_;
    ++(info_->copy_assign);
    return *this;
  }
  This& operator=(This&& o) noexcept {
    info_ = o.info_;
    ++(info_->move_assign);
    return *this;
  }
  Info* info_;
};

template<typename M>
std::unordered_set<typename M::key_type> GetKeys(const M& m) {
  std::unordered_set<typename M::key_type> output;
  for (auto& e : m) {
    output.insert(e.first);
  }
  return output;
}

TEST(LazyMapTest, Basic) {
  lazy_map<int, int> m = {{1, 10}, {2, 20}, {3, 30}};
  EXPECT_EQ(3, m.size());
  m.insert(4, 40);
  EXPECT_EQ(4, m.size());
  EXPECT_EQ(40, m.at(4));
  m.insert_or_assign(3, 50);
  EXPECT_EQ(50, m.at(3));
  EXPECT_EQ(4, m.size());
  EXPECT_TRUE(m.contains(1));
  m.erase(1);
  EXPECT_FALSE(m.contains(1));
  EXPECT_EQ(3, m.size());
  EXPECT_TRUE(m.contains(2));
  m.clear();
  EXPECT_FALSE(m.contains(2));
  EXPECT_EQ(0, m.size());
  m.insert({10, 20 + 30});
  EXPECT_EQ(1, m.size());
  EXPECT_NE(m.find(10), m.end());
  EXPECT_EQ(50, m.find(10)->second);
}

TEST(LazyMapTest, PreserveValueSemantics) {
  lazy_map<int, int> m1 = {{1, 10}, {2, 20}, {3, 30}};
  auto m2 = m1;
  EXPECT_EQ(3, m2.size());
  m2.insert(4, 40);
  EXPECT_EQ(4, m2.size());
  EXPECT_EQ(40, m2.at(4));
  EXPECT_EQ(3, m1.size());
  EXPECT_FALSE(m1.contains(4));
  m1.insert_or_assign(3, 50);
  EXPECT_EQ(50, m1.at(3));
  EXPECT_EQ(3, m1.size());
  EXPECT_EQ(30, m2.at(3));
  EXPECT_EQ(4, m2.size());
  auto m3 = m2;
  EXPECT_EQ(4, m3.size());
  EXPECT_EQ(10, m3.at(1));
  EXPECT_TRUE(m3.contains(4));
  EXPECT_FALSE(m3.contains(5));
  m3.erase(1);
  EXPECT_FALSE(m3.contains(1));
  EXPECT_TRUE(m1.contains(1));
  EXPECT_TRUE(m2.contains(1));
  EXPECT_EQ(3, m3.size());
  EXPECT_EQ(3, m1.size());
  EXPECT_EQ(4, m2.size());
  m3.clear();
  EXPECT_EQ(0, m3.size());
  EXPECT_EQ(3, m1.size());
  EXPECT_EQ(4, m2.size());
}


TEST(LazyMapTest, DetachmentTest) {
  lazy_map<int, int> m1 = {{1, 10}, {2, 20}, {3, 30}};
  auto m2 = m1;
  m2.insert(4, 40);
  auto m3 = m2;
  m3.insert(5, 50);
  m3.erase(3);
  EXPECT_EQ((std::unordered_set<int> {1, 2, 3, 4}), GetKeys(m2));
  EXPECT_EQ((std::unordered_set<int> {1, 2, 4, 5}), GetKeys(m3));
  EXPECT_TRUE(m2.detach());
  EXPECT_FALSE(m2.detach());
  EXPECT_TRUE(m2.is_detached());
  EXPECT_TRUE(m3.detach());
  auto m4 = m3;
  m4.insert(6, 60);
  EXPECT_TRUE(m4.detach());
}

TEST(LazyMapTest, IteratorTest) {
  lazy_map<int, int> m1 = {{1, 10}, {2, 20}, {3, 30}};
  auto m2 = m1;
  m2.insert(4, 40);
  m2.detach();
  std::set<std::pair<int, int>> v1(m2.begin(), m2.end());
  std::set<std::pair<int, int>> v2 {{1, 10}, {2, 20}, {3, 30}, {4, 40}};
  EXPECT_EQ(v2, v1);
  EXPECT_FALSE(m2.detach());
  EXPECT_FALSE(m1.detach());
  auto m3 = m2;
  m2.insert(5, 50);
  std::unordered_set<int> v3;
  for (const auto& item : m2) {
    v3.insert(item.second - item.first);
  }
  EXPECT_EQ((std::unordered_set<int> {9, 18, 27, 36, 45}), v3);
  auto m4 = m3;
  m4.erase(3);
  m4.insert_or_assign(2, 21);
  EXPECT_EQ(std::unordered_set<int>({4, 1, 2}), GetKeys(m4));
  auto m5 = m4;
  m5.clear();
  EXPECT_EQ(3, GetKeys(m4).size());
  m5 = m4;
  m5.insert(12, 33);
  EXPECT_EQ(std::unordered_set<int>({4, 1, 2, 12}), GetKeys(m5));
  m5.erase(12);
  auto m6 = m5;
  EXPECT_EQ(2, m6.get_depth());
  m6.insert(13, 33);
  EXPECT_EQ(std::unordered_set<int>({4, 1, 2, 13}), GetKeys(m6));
  EXPECT_EQ(3, m6.get_depth());
  lazy_map<int, int> m7({{1, 10}});
  auto m8 = m7;
  m7.erase(1);
  EXPECT_EQ(std::unordered_set<int>({}), GetKeys(m7));
}

TEST(LazyMapTest, CopyMoveInsertion) {
  quick::lazy_map<int, CopyMoveCounter> m;
  CopyMoveCounter::Info info;
  m.insert(10, CopyMoveCounter(&info));
  EXPECT_EQ(1, info.moves());
  EXPECT_EQ(0, info.copies());
  EXPECT_EQ(1, info.value_ctr);
  info.reset();
  m.emplace(20, &info);
  EXPECT_EQ(0, info.moves());
  EXPECT_EQ(0, info.copies());
  EXPECT_EQ(1, info.value_ctr);
  CopyMoveCounter c1(&info);
  info.reset();
  m.insert_or_assign(10, std::move(c1));
  EXPECT_EQ(1, info.move_assign);
  EXPECT_EQ(1, info.total());
}

TEST(LazyMapTest, MoveMethod) {
  lazy_map<int, vector<int>> m = {{10, {1, 2, 3}}, {20, {4, 5, 6}}};
  auto v = m.move(20);
  EXPECT_EQ((vector<int>{4, 5, 6}), v);
  v.push_back(7);
  m.insert_or_assign(20, std::move(v));
  EXPECT_EQ((vector<int>{4, 5, 6, 7}), m.at(20));
  auto m2 = m;
  auto v2 = m.move(10);
  EXPECT_EQ((vector<int>{1, 2, 3}), v2);
  EXPECT_EQ((vector<int>{1, 2, 3}), m.at(10));
  v2.push_back(9);
  m.insert_or_assign(10, std::move(v2));
  EXPECT_EQ((vector<int>{1, 2, 3, 9}), m.at(10));
  EXPECT_EQ((vector<int>{1, 2, 3}), m2.at(10));
}

TEST(LazyMapTest, MoveMethodPerf) {
  quick::lazy_map<int, CopyMoveCounter> m;
  CopyMoveCounter::Info info;
  {
    m.insert(10, CopyMoveCounter(&info));
    m.insert(20, CopyMoveCounter(&info));
    EXPECT_EQ(0, info.copies());
  }
  info.reset();
  {
    // When value for a key is moved from map, and the value is not shared
    // by other objects, then value should not be copied.
    auto v = m.move(10);
    EXPECT_EQ(1, info.moves());
    EXPECT_EQ(0, info.copies());
    EXPECT_EQ(0, info.value_ctr);
  }
  info.reset();
  {
    // Moved-from value for key 10 is assigned a different value.
    // This operation should not invole copy.
    m.insert_or_assign(10, CopyMoveCounter(&info));
    EXPECT_EQ(1, info.move_assign);
    EXPECT_EQ(1, info.value_ctr);
    EXPECT_EQ(0, info.copies());
    EXPECT_EQ(2, info.total());
  }
  {
    // value for key=10 is moved from map but this value is shared by another
    // object, hence it will be copied.
    auto m2 = m;
    info.reset();
    auto v2 = m.move(10);  
    EXPECT_EQ(0, info.moves());
    EXPECT_EQ(1, info.copies());
    EXPECT_EQ(0, info.value_ctr);
  }
  info.reset();
  {
    // Here map is not shared. It's earlier shareholder (m2) is out of scope.
    // Hence moving value for key 10 will not fall back to coping.
    auto v = m.move(10);
    EXPECT_EQ(1, info.moves());
    EXPECT_EQ(0, info.copies());
    EXPECT_EQ(0, info.value_ctr);
  }
  {
    // When map value is not shared by another object, 'move_only' method
    // returns non-empty value for key 10.
    auto optional_value = m.move_only(10);
    EXPECT_TRUE(optional_value.has_value());
    auto m2 = m;
    // When map value is not shared by another object, 'move_only' method
    // returns empty std::optional for key 10.
    auto optional_value2 = m.move_only(10);
    EXPECT_FALSE(optional_value2.has_value());
  }
}

TEST(LazyMapTest, NonCopiableValueType) {
  using std::unique_ptr;
  using std::make_unique;
  lazy_map<int, unique_ptr<int>> m;
  m.insert(10, nullptr);
  m.insert(20, make_unique<int>(6));
  auto v = m.move_only(20);
  EXPECT_TRUE(v.has_value());
  EXPECT_TRUE((std::is_same<unique_ptr<int>&, decltype(v.value())>::value));
  EXPECT_NE(v.value(), nullptr);
  EXPECT_EQ(6, *v.value());
  *v.value() = 7;
  m.insert_or_assign(20, std::move(v.value()));
  EXPECT_NE(nullptr, m.at(20));
  EXPECT_EQ(7, *m.at(20));
  auto m2 = m;
  auto v2 = m.move_only(20);
  EXPECT_FALSE(v2.has_value());
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
