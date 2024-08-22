#include <cstddef>

extern "C" {
void *__tsan_get_current_fiber(void);
void *__tsan_create_fiber(unsigned flags);
void __tsan_destroy_fiber(void *fiber);
void __tsan_switch_to_fiber(void *fiber, unsigned flags);
}

class SanitizerContext {
public:
  void Setup(void *, size_t) { current_fiber_ = __tsan_create_fiber(0); }

  void Run() { CleanParent(); }

  // This function must be always inlined because of the
  // https://github.com/llvm/llvm-project/blob/main/compiler-rt/include/sanitizer/tsan_interface.h#L145C2-L145C2
  __attribute__((always_inline)) inline void
  BeforeSwap(SanitizerContext &swap_to) {
    current_fiber_ = __tsan_get_current_fiber();
    __tsan_switch_to_fiber(swap_to.current_fiber_, 0);
  }

  void AfterSwap() { CleanParent(); }

  // This function must be always inlined because of the
  // https://github.com/llvm/llvm-project/blob/main/compiler-rt/include/sanitizer/tsan_interface.h#L145C2-L145C2
  __attribute__((always_inline)) inline void
  BeforeExit(SanitizerContext &exit_to) {
    exit_to.parent_ = this;
    __tsan_switch_to_fiber(exit_to.current_fiber_, 0);
  }

private:
  SanitizerContext *parent_{nullptr};

  void *current_fiber_;

  void CleanParent() {
    if (parent_ != nullptr) {
      __tsan_destroy_fiber(parent_->current_fiber_);
      parent_ = nullptr;
    }
  }
};
