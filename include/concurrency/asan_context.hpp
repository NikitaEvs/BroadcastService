#include <cstddef>

extern "C" {
void __sanitizer_start_switch_fiber(void **fake_stack_save, const void *bottom,
                                    size_t size);
void __sanitizer_finish_switch_fiber(void *fake_stack_save,
                                     const void **bottom_old, size_t *size_old);
}

class SanitizerContext {
public:
  void Setup(void *stack_data, size_t stack_size) {
    stack_data_ = stack_data;
    stack_size_ = stack_size;
  }

  void Run() {
    __sanitizer_finish_switch_fiber(nullptr, &(parent_->stack_data_),
                                    &(parent_->stack_size_));
  }

  void BeforeSwap(SanitizerContext &swap_to) {
    swap_to.parent_ = this;
    __sanitizer_start_switch_fiber(&fake_stack_save_, swap_to.stack_data_,
                                   swap_to.stack_size_);
  }

  void AfterSwap() {
    __sanitizer_finish_switch_fiber(fake_stack_save_, &(parent_->stack_data_),
                                    &(parent_->stack_size_));
  }

  void BeforeExit(SanitizerContext &exit_to) {
    exit_to.parent_ = this;
    // Notify ASAN about destruction of the stack
    __sanitizer_start_switch_fiber(nullptr, exit_to.stack_data_,
                                   exit_to.stack_size_);
  }

private:
  SanitizerContext *parent_{nullptr};
  const void *stack_data_;
  size_t stack_size_;
  void *fake_stack_save_{nullptr};
};
