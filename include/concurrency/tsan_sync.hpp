#pragma once

#include <atomic>

// Instrumentation for the TSAN for using with epoll
// https://nullprogram.com/blog/2022/10/03/

// For the compilation with g++ without warnings
#ifndef __has_feature
#define __has_feature(x) false
#endif

#if defined(__SANITIZER_THREAD__) || __has_feature(thread_sanitizer)
#define USE_TSAN
#endif

#if defined(USE_TSAN)
#define TSAN_SYNC std::atomic<int32_t> tsan_sync_atomic_;
#define TSAN_RELEASE tsan_sync_atomic_.store(0, std::memory_order_release);
#define TSAN_ACQUIRE tsan_sync_atomic_.load(std::memory_order_acquire);
#else
#define TSAN_SYNC
#define TSAN_RELEASE
#define TSAN_ACQUIRE
#endif
