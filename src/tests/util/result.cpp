#include <gtest/gtest.h>
#include <system_error>

#include "util/noncopyable.hpp"
#include "util/result.hpp"

TEST(Result, SmokeTest) {
  Result<int32_t> result;
  result.SetValue(5);
  ASSERT_TRUE(result.HasValue());
  ASSERT_FALSE(result.HasError());
  ASSERT_EQ(5, result.Value());
}

TEST(Result, Value) {
  auto result = Ok(5);
  ASSERT_TRUE(result.HasValue());
  ASSERT_FALSE(result.HasError());
  ASSERT_EQ(5, result.Value());
}

TEST(Result, Err) {
  auto result = Err<std::monostate>(make_error_code(Error::internal));
  ASSERT_FALSE(result.HasValue());
  ASSERT_TRUE(result.HasError());
  ASSERT_EQ(Error::internal, result.Error());
}

struct Unit : private NonCopyable {
  Unit() {}
  Unit(Unit &&other) { value = other.value; }
  int value;
};

TEST(Result, NonCopyable) {
  Unit unit;
  unit.value = 5;
  auto result = Ok(std::move(unit));

  ASSERT_TRUE(result.HasValue());
  ASSERT_FALSE(result.HasError());

  auto unit_moved = result.Value();
  ASSERT_EQ(5, unit_moved.value);
}
