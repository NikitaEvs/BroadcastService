#pragma once

#include "runtime/executor.hpp"
#include "runtime/routine.hpp"
#include "runtime/suspend_strategy.hpp"

void Go(IExecutorPtr executor, Routine routine);

// Must be only called from the fiber
void Go(Routine routine);

void Suspend(ISuspendStrategyPtr suspend_strategy);

void Yield();
