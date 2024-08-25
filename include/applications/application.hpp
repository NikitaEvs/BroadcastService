#pragma once

#include <functional>

class IApplication {
public:
  using SignalHandler = std::function<void(int)>;

  virtual ~IApplication() = default;

  virtual void Start() = 0;
  virtual void Run() = 0;
  virtual void Stop() = 0;
  virtual void Shutdown() = 0;

  virtual SignalHandler GetSignalHandler() = 0;
};
