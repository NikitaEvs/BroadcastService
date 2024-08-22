#pragma once

#include <algorithm>
#include <unordered_set>
#include <vector>

// VectorSetUnion(L, R): L = L U R
template <typename T>
void VectorSetUnion(std::vector<T> &lhs_set, const std::vector<T> &rhs_set) {
  // TODO: Optimise
  std::unordered_set<T> lhs_hashes;
  lhs_hashes.reserve(lhs_set.size());
  lhs_hashes.insert(lhs_set.begin(), lhs_set.end());

  for (auto rhs_item : rhs_set) {
    if (lhs_hashes.count(rhs_item) == 0) {
      lhs_set.push_back(rhs_item);
    }
  }

  // for (auto value : rhs_set) {
  //   if (std::find(lhs_set.begin(), lhs_set.end(), value) == lhs_set.end()) {
  //     lhs_set.push_back(value);
  //   }
  // }
}

// SetUnion(L, R): L = L U R
template <typename SetT>
inline void SetUnion(SetT &lhs_set, const SetT &rhs_set) {
  for (auto rhs_item : rhs_set) {
    lhs_set.insert(rhs_item);
  }
}

// VectorSetLessOrEquals(L, R): return L <= R
template <typename T>
bool VectorSetLessOrEquals(const std::vector<T> &lhs_set,
                           const std::vector<T> &rhs_set) {
  // TODO: Optimise
  std::unordered_set<T> rhs_hashes;
  rhs_hashes.reserve(rhs_set.size());
  rhs_hashes.insert(rhs_set.begin(), rhs_set.end());

  for (auto lhs_item : lhs_set) {
    if (rhs_hashes.count(lhs_item) == 0) {
      return false;
    }
  }
  return true;
}

// SetLessOrEquals(L, R): return L <= R
template <typename SetT>
bool SetLessOrEquals(const SetT &lhs_set, const SetT &rhs_set) {
  for (auto lhs_item : lhs_set) {
    if (rhs_set.count(lhs_item) == 0) {
      return false;
    }
  }
  return true;
}
