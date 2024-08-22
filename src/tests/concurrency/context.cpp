#include "concurrency/context.hpp"
#include "runtime/runnable.hpp"

#include <cstdint>
#include <gtest/gtest.h>

#include <iostream>
#include <sstream>

Context *oldContextPtr = nullptr;
Context *newContextPtr = nullptr;

class Hello : public IRunnable {
public:
  void Run() final {
    std::cout << "hello" << std::endl;
    SwapContext(newContextPtr, oldContextPtr);
    std::cout << "everyone" << std::endl;
    SwapContext(newContextPtr, oldContextPtr);
  }
};

extern "C" void ConRun(void *, void *, void *, void *, void *, void *,
                       void *runnable_raw);

extern "C" void ConRun(void *, void *, void *, void *, void *, void *,
                       void *runnable_raw) {
  auto *runnable = reinterpret_cast<IRunnable *>(runnable_raw);
  runnable->Run();
}

static constexpr size_t kStackSize = 64 * 4096;

TEST(Stack, SmokeTest) {
  auto stack = Stack::AllocateStack(kStackSize);
  auto view = stack.MutableView();
  ASSERT_EQ(kStackSize, view.size);
  ASSERT_NE(nullptr, view.ptr);
}

TEST(Stack, WriteRead) {
  auto stack = Stack::AllocateStack(kStackSize);
  auto view = stack.MutableView();
  auto *ptr = view.ptr;
  ptr[0] = 'a';
  ptr[1] = 'b';
  ptr[2] = 'c';

  char read = ptr[1];
  ASSERT_EQ('b', read);
}

struct Inverter : public IRunnable {
  FiberContext *previous;
  FiberContext current;
  bool flag = false;

  Inverter(StackMutableView view, FiberContext *old_context)
      : previous(old_context), current(view, this) {}

  void Start() { previous->SwapTo(current); }

  void Run() final {
    flag = true;
    current.ExitTo(*previous);
  }
};

TEST(FiberContext, Inverter) {
  auto stack = Stack::AllocateStack(kStackSize);
  auto view = stack.MutableView();

  FiberContext old_context;

  Inverter inverter(view, &old_context);
  ASSERT_FALSE(inverter.flag);
  inverter.Start();
  ASSERT_TRUE(inverter.flag);
}

struct SimpleCoroutine : public IRunnable {
  FiberContext *previous;
  FiberContext current;
  int32_t value = 0;

  SimpleCoroutine(StackMutableView view, FiberContext *old_context)
      : previous(old_context), current(view, this) {}

  void Resume() { previous->SwapTo(current); }

  void Suspend() { current.SwapTo(*previous); }

  void Exit() { current.ExitTo(*previous); }

  void Run() final {
    Suspend();
    value = 1;
    Suspend();
    value = 2;
    Suspend();
    value = 3;
    Exit();
  }
};

TEST(FiberContext, Coroutine) {
  auto stack = Stack::AllocateStack(kStackSize);
  auto view = stack.MutableView();

  FiberContext old_context;

  SimpleCoroutine coro(view, &old_context);

  ASSERT_EQ(0, coro.value);
  coro.Resume();
  ASSERT_EQ(0, coro.value);
  coro.Resume();
  ASSERT_EQ(1, coro.value);
  coro.Resume();
  ASSERT_EQ(2, coro.value);
  coro.Resume();
  ASSERT_EQ(3, coro.value);
}