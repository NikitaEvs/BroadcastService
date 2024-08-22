#pragma once

/**
 * Context code and sanitizer support is based on the
 * https://graphitemaster.github.io/fibers/
 */

// For the compilation with g++ without warnings
#ifndef __has_feature
#define __has_feature(x) false
#endif

#if defined(__SANITIZE_ADDRESS__) || __has_feature(address_sanitizer)
#include "asan_context.hpp"
#define USE_ASAN
#endif

#if defined(__SANITIZER_THREAD__) || __has_feature(thread_sanitizer)
#include "tsan_context.hpp"
#define USE_TSAN
#endif

#if !defined(USE_TSAN) && !defined(USE_ASAN)
#include "sanitizer_context.hpp"
#endif

#include "runtime/runnable.hpp"
#include "util/noncopyable.hpp"
#include "util/nonmovable.hpp"

#include <cstddef>
#include <functional>

struct Context {
  void *rip = nullptr;
  void *rsp = nullptr;
};

extern "C" void SetContext(Context *context, IRunnable *runnable);
extern "C" void SwapContext(Context *old_context, Context *new_context);

// Seventh argument will be taken from the stack
extern "C" void ContextRun(void *, void *, void *, void *, void *, void *,
                           void *runnable_raw);

struct StackMutableView {
  char *ptr;
  size_t size;
};

class Stack : private NonCopyable {
public:
  ~Stack();
  Stack(Stack &&other) noexcept;
  Stack &operator=(Stack &&other) noexcept;

  // TODO: Implement Stack pooling
  static Stack AllocateStack(size_t size);
  [[nodiscard]] StackMutableView MutableView();

private:
  char *memory_;
  size_t size_;

  Stack(char *memory, size_t size);

  static char *AllocateMemory(size_t size);
  static void ReleaseMemory(char *memory, size_t size);
};

class FiberContext : public IRunnable, private NonCopyable {
public:
  FiberContext() = default;
  FiberContext(StackMutableView view, IRunnablePtr runnable);
  ~FiberContext() = default;

  void SwapTo(FiberContext &other);

  [[noreturn]] void ExitTo(FiberContext &other);

  void Run() final;

private:
  SanitizerContext sanitizer_context_;
  Context context_;
  IRunnablePtr runnable_{nullptr};

  void SetupContext(StackMutableView view);

  bool IsValidForSwapping() const;
};

using FiberContextPtr = FiberContext *;
