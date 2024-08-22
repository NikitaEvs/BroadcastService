#include <cstddef>

// SanitizerContext for the compilation without sanitizers
class SanitizerContext {
public:
  void Setup(void *, size_t) {}

  void Run() {}

  void BeforeSwap(SanitizerContext &) {}

  void AfterSwap() {}

  void BeforeExit(SanitizerContext &) {}
};
