#pragma once

#include <random>

template <typename IntegerT> IntegerT RandInt(IntegerT from, IntegerT to) {
  static std::random_device random_device;
  static std::mt19937 mt(random_device());

  std::uniform_int_distribution<IntegerT> distribution(from, to);

  return distribution(mt);
}
