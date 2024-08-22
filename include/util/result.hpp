#pragma once

#include <system_error>
#include <type_traits>
#include <variant>

// Custom std::error_code implementation details are taken from:
// http://blog.think-async.com/2010/04/system-error-support-in-c0x-part-5.html
enum Error {
  internal = 1,
};

class ErrorImpl : public std::error_category {
public:
  using std::error_category::equivalent;

  const char *name() const noexcept final { return "Error"; }
  virtual std::string message(int ev) const {
    switch (ev) {
    case Error::internal: {
      return "Internal error";
    }
    default: {
      return "Unknown error";
    }
    }
  }
  bool equivalent(const std::error_code & /*code*/,
                  int /*condition*/) const noexcept final {
    return false;
  }
};

inline const std::error_category &ErrorCategory() {
  static ErrorImpl instance;
  return instance;
}

inline std::error_condition make_error_condition(Error e) {
  return std::error_condition(static_cast<int>(e), ErrorCategory());
}

inline std::error_code make_error_code(Error e) {
  return std::error_code(static_cast<int>(e), ErrorCategory());
}

namespace std {
template <> struct is_error_condition_enum<Error> : public true_type {};
} // namespace std

// Inspiried by std::expected proposal
template <typename T> class Result {
public:
  using ValueType = T;

  Result() = default;

  explicit Result(T value) {
    placeholder_.template emplace<T>(std::move(value));
  }

  explicit Result(std::error_code ec) {
    placeholder_.template emplace<std::error_code>(ec);
  }

  Result(const Result &other) { placeholder_ = other.placeholder_; }

  Result &operator=(const Result &other) {
    placeholder_ = other.placeholder_;
    return *this;
  }

  Result(Result &&other) noexcept {
    placeholder_ = std::move(other.placeholder_);
  }

  Result &operator=(Result<T> &&other) noexcept {
    placeholder_ = std::move(other.placeholder_);
    return *this;
  }

  bool HasValue() const { return placeholder_.index() == kValueIndex; }

  bool HasError() const { return placeholder_.index() == kErrorIndex; }

  T Value() { return std::move(std::get<T>(placeholder_)); }

  void SetValue(T value) { placeholder_.template emplace<T>(std::move(value)); }

  std::error_code Error() const {
    return std::move(std::get<std::error_code>(placeholder_));
  }

  void SetError(std::error_code ec) {
    placeholder_.template emplace<std::error_code>(ec);
  }

private:
  static constexpr size_t kErrorIndex = 0;
  static constexpr size_t kValueIndex = 1;

  std::variant<std::error_code, T> placeholder_;
};

template <typename T> static Result<T> Ok(T value) {
  return Result(std::move(value));
}

template <typename T> static Result<T> Err(std::error_code ec) {
  return Result<T>(ec);
}

namespace traits {
template <typename T> struct Type {};

template <typename T> struct Type<Result<T>> {
  using InnerType = T;
};
} // namespace traits

template <typename Result>
using GetType = typename traits::Type<Result>::InnerType;
