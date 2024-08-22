#pragma once

class NonMoveable {
public:
  NonMoveable(NonMoveable &&) = delete;
  NonMoveable &operator=(NonMoveable &&) = delete;

protected:
  NonMoveable() = default;
  ~NonMoveable() = default;
};
