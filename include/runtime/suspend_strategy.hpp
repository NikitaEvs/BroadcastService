#pragma once

class Fiber;

class ISuspendStrategy {
public:
  virtual ~ISuspendStrategy() = default;
  virtual void Suspend(Fiber *fiber) = 0;
};

using ISuspendStrategyPtr = ISuspendStrategy *;
