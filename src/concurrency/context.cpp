#include "concurrency/context.hpp"

#include <cassert>
#include <cstdint>
#include <sys/mman.h>

#include "util/syscall.hpp"

// Seventh argument will be taken from the stack
extern "C" void ContextRun(void *, void *, void *, void *, void *, void *,
                           void *runnable_raw) {
  auto *runnable = reinterpret_cast<IRunnable *>(runnable_raw);
  runnable->Run();
}

Stack::~Stack() { ReleaseMemory(memory_, size_); }

Stack::Stack(Stack &&other) noexcept {
  memory_ = other.memory_;
  size_ = other.size_;
  other.memory_ = nullptr;
  other.size_ = 0;
}

Stack &Stack::operator=(Stack &&other) noexcept {
  ReleaseMemory(memory_, size_);
  memory_ = other.memory_;
  size_ = other.size_;
  other.memory_ = nullptr;
  other.size_ = 0;
  return *this;
}

Stack Stack::AllocateStack(size_t size) {
  char *memory = Stack::AllocateMemory(size);
  return {memory, size};
}

StackMutableView Stack::MutableView() { return {memory_, size_}; }

Stack::Stack(char *memory, size_t size) : memory_(memory), size_(size) {}

char *Stack::AllocateMemory(size_t size) {
  auto *memory = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                      MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
  if (memory == MAP_FAILED) {
    CANNOT_FAIL(-1, "Cannot allocate memory");
  }
  return reinterpret_cast<char *>(memory);
}

void Stack::ReleaseMemory(char *memory, size_t size) {
  if (memory != nullptr) {
    auto *addr = reinterpret_cast<void *>(memory);
    CANNOT_FAIL(munmap(addr, size), "Cannot deallocate memory");
  }
}

FiberContext::FiberContext(StackMutableView view, IRunnablePtr runnable)
    : runnable_(runnable) {
  SetupContext(view);
}

void FiberContext::SwapTo(FiberContext &other) {
  assert(other.IsValidForSwapping() && "Switching to the invalid context");
  sanitizer_context_.BeforeSwap(other.sanitizer_context_);
  SwapContext(&context_, &other.context_);
  sanitizer_context_.AfterSwap();
}

void FiberContext::ExitTo(FiberContext &other) {
  sanitizer_context_.BeforeExit(other.sanitizer_context_);
  SwapContext(&context_, &other.context_);
  std::abort();
}

void FiberContext::Run() {
  sanitizer_context_.Run();
  runnable_->Run();
}

void FiberContext::SetupContext(StackMutableView view) {
  char *stack_end = view.ptr + view.size;

  context_.rip = reinterpret_cast<void *>(ContextRun);
  context_.rsp = stack_end;

  SetContext(&context_, this);

  sanitizer_context_.Setup(stack_end, view.size);
}

bool FiberContext::IsValidForSwapping() const {
  return context_.rip != nullptr;
}