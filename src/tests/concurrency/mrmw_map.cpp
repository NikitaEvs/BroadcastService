#include <gtest/gtest.h>
#include <mutex>

#include "concurrency/futex.hpp"
#include "concurrency/mrmw_map.hpp"
#include "concurrency/spinlock.hpp"
#include "runtime/thread_pool.hpp"

using KeyT = size_t;
using ValueT = size_t;

TEST(MRMWMap, SmokeTestMutex) {
  MRMWUnorderedMap<KeyT, ValueT, std::mutex> map;
  map.Set(1, 2);
  auto value = map.Take(1);
  ASSERT_TRUE(value.has_value());
  ASSERT_EQ(2, *value);
}

TEST(MRMWMap, SmokeTestSpinlock) {
  MRMWUnorderedMap<KeyT, ValueT, SpinLock> map;
  map.Set(1, 2);
  auto value = map.Take(1);
  ASSERT_TRUE(value.has_value());
  ASSERT_EQ(2, *value);
}

template <typename Map>
void WorkloadTest(Map &map, size_t num_producers, size_t num_consumers,
                  size_t num_values) {
  ThreadPool thread_pool(num_producers + num_consumers);
  thread_pool.Start();

  for (size_t producer = 0; producer < num_producers; ++producer) {
    thread_pool.Execute(Lambda::Create([&map, num_values]() {
      for (size_t i = 0; i < num_values; ++i) {
        map.Set(i, i);
      }
    }));
  }

  std::atomic<size_t> num_extractions{0};
  for (size_t consumer = 0; consumer < num_consumers; ++consumer) {
    thread_pool.Execute(Lambda::Create([&map, &num_extractions, num_values]() {
      for (size_t i = 0; i < num_values; ++i) {
        auto value = map.Take(i);
        if (value.has_value()) {
          num_extractions.fetch_add(1);
          ASSERT_EQ(i, *value);
        }
      }
    }));
  }

  thread_pool.Wait();

  ASSERT_GE(num_extractions.load(), num_values / 2);

  thread_pool.SignalStop();
  thread_pool.WaitForStop();
}

TEST(MRMWMap, WorkloadMutex) {
  MRMWUnorderedMap<KeyT, ValueT, std::mutex> map;
  WorkloadTest(map, 1, 1, 100);
  WorkloadTest(map, 3, 3, 100);
  WorkloadTest(map, 3, 3, 10000);
  WorkloadTest(map, 5, 5, 100000);
}

TEST(MRMWMap, WorkloadSpinLock) {
  MRMWUnorderedMap<KeyT, ValueT, SpinLock> map;
  WorkloadTest(map, 1, 1, 100);
  WorkloadTest(map, 3, 3, 100);
  WorkloadTest(map, 3, 3, 10000);
  WorkloadTest(map, 5, 5, 100000);
}

TEST(MRMWMap, Drain) {
  constexpr size_t number_inserts = 100;

  MRMWUnorderedMap<KeyT, ValueT, SpinLock> map;

  for (size_t i = 0; i < number_inserts; ++i) {
    map.Set(i, i);
  }

  auto values = map.Drain();
  ASSERT_EQ(number_inserts, values.size());

  std::sort(values.begin(), values.end());

  for (size_t i = 0; i < number_inserts; ++i) {
    ASSERT_EQ(i, values[i]);
  }
}
