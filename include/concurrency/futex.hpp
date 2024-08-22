#pragma once

#include <atomic>

using Key = uint32_t *;

void AtomicWait(std::atomic<uint32_t> &atomic, uint32_t value);

Key AtomicAddr(std::atomic<uint32_t> &atomic);

void AtomicWakeOne(Key key);

void AtomicWakeAll(Key key);
