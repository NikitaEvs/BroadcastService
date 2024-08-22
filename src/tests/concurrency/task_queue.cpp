#include <gtest/gtest.h>

#include "concurrency/task_queue.hpp"
#include "runtime/thread_pool.hpp"
#include "util/instrusive_list.hpp"

struct Task : public IntrusiveNode<Task> {
  int32_t number{0};

  explicit Task(int32_t number) : number(number) {}
  Task() = default;
};

TEST(TaskQueue, SmokeTest) {
  BatchedLFQueue<Task> queue;

  Task task(5);
  queue.Push(&task);

  auto batch = queue.TryPopAll();

  ASSERT_EQ(1, batch.Size());
  auto *node = batch.PopFront();

  ASSERT_EQ(5, node->number);
}

TEST(TaskQueue, Fifo) {
  BatchedLFQueue<Task> queue;

  constexpr size_t num_tasks = 100;
  std::vector<Task> tasks(num_tasks);

  for (size_t i = 0; i < num_tasks; ++i) {
    tasks[i].number = static_cast<int32_t>(i);
  }

  for (auto &task : tasks) {
    queue.Push(&task);
  }

  auto batch = queue.TryPopAll();

  ASSERT_EQ(num_tasks, batch.Size());
  int32_t counter = 0;
  while (auto *node = batch.PopFront()) {
    ASSERT_EQ(counter, node->number);
    ++counter;
  }
}

TEST(TaskQueue, PushList) {
  BatchedLFQueue<Task> queue;

  constexpr size_t num_tasks_first = 100;
  constexpr size_t num_tasks_second = 100;
  std::vector<Task> tasks(num_tasks_first + num_tasks_second);

  for (size_t i = 0; i < tasks.size(); ++i) {
    tasks[i].number = static_cast<int32_t>(i);
  }

  for (size_t i = 0; i < num_tasks_first; ++i) {
    queue.Push(&tasks[i]);
  }

  IntrusiveList<Task> list;
  for (size_t i = num_tasks_first; i < tasks.size(); ++i) {
    list.PushBack(&tasks[i]);
  }

  queue.Push(std::move(list));

  auto batch = queue.TryPopAll();

  ASSERT_EQ(tasks.size(), batch.Size());
  int32_t counter = 0;
  while (auto *node = batch.PopFront()) {
    ASSERT_EQ(counter, node->number);
    ++counter;
  }
}

TEST(TaskQueue, MultiProducerMultiConsumer) {
  BatchedLFQueue<Task> queue;

  constexpr size_t num_producers = 50;
  constexpr size_t num_consumers = 50;
  constexpr size_t num_pushes = 1000;
  constexpr size_t number = 5;

  constexpr auto sum_tasks = num_producers * num_pushes;
  std::vector<Task> tasks(sum_tasks);
  for (auto &task : tasks) {
    task.number = number;
  }

  std::atomic<size_t> sum_number{0};
  std::atomic<size_t> consumed{0};

  ThreadPool thread_pool(10);
  thread_pool.Start();

  for (size_t num_producer = 0; num_producer < num_producers; ++num_producer) {
    thread_pool.Execute(
        Lambda::Create([&queue, &tasks, num_producer, num_pushes]() {
          for (size_t num_push = 0; num_push < num_pushes; ++num_push) {
            queue.Push(&tasks[num_producer * num_pushes + num_push]);
          }
        }));
  }

  for (size_t num_consumer = 0; num_consumer < num_consumers; ++num_consumer) {
    thread_pool.Execute(Lambda::Create([&queue, &sum_number, &consumed]() {
      while (consumed.load() < sum_tasks) {
        auto batch = queue.TryPopAll();
        size_t num_tasks = 0;
        while (auto *node = batch.PopFront()) {
          sum_number.fetch_add(static_cast<size_t>(node->number));
          ++num_tasks;
        }
        consumed.fetch_add(num_tasks);
      }
    }));
  }

  thread_pool.Wait();

  ASSERT_EQ(sum_tasks * number, sum_number.load());
  ASSERT_EQ(sum_tasks, consumed.load());

  thread_pool.SignalStop();
  thread_pool.WaitForStop();
}
