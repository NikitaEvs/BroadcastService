#include <gtest/gtest.h>
#include <system_error>
#include <thread>

#include "concurrency/async.hpp"
#include "concurrency/combine.hpp"
#include "concurrency/contract.hpp"
#include "runtime/test_executor.hpp"
#include "runtime/thread_pool.hpp"
#include "util/result.hpp"

using namespace std::chrono_literals;

TEST(Future, SmokeTest) {
  auto [f, p] = MakeContract<int32_t>();
  std::move(p).SetValue(5);
  auto result = f.Get();
  ASSERT_TRUE(result.HasValue());
  ASSERT_EQ(5, result.Value());
}

TEST(Future, Executor) {
  ThreadPool thread_pool(5);
  thread_pool.Start();

  auto [f, p] = MakeContract<int32_t>();

  thread_pool.Execute(Lambda::Create([p = p]() mutable {
    std::this_thread::sleep_for(300ms);
    std::move(p).SetValue(5);
  }));

  auto result = f.Get();

  ASSERT_TRUE(result.HasValue());
  ASSERT_EQ(5, result.Value());

  thread_pool.Wait();
  thread_pool.SignalStop();
  thread_pool.WaitForStop();
}

TEST(Future, Then) {
  ThreadPool thread_pool(5);
  thread_pool.Start();

  auto [f, p] = MakeContract<int32_t>();

  thread_pool.Execute(Lambda::Create([p = p]() mutable {
    std::this_thread::sleep_for(300ms);
    std::move(p).SetValue(5);
  }));

  auto result = f.Then([](int32_t value) { return Ok(value * 2); }).Get();

  ASSERT_TRUE(result.HasValue());
  ASSERT_EQ(10, result.Value());

  thread_pool.Wait();
  thread_pool.SignalStop();
  thread_pool.WaitForStop();
}

TEST(Future, Via) {
  TestExecutor test_executor;

  ThreadPool thread_pool(5);
  thread_pool.Start();

  auto [f, p] = MakeContract<bool>();

  thread_pool.Execute(
      Lambda::Create([p = p]() mutable { std::move(p).SetValue(true); }));

  std::atomic<bool> result = false;
  f.Via(&test_executor)
      .Then([&result](int32_t value) {
        result.store(value);
        return Ok(value);
      })
      .Detach();

  std::this_thread::sleep_for(100ms);
  ASSERT_FALSE(result.load());
  ASSERT_TRUE(test_executor.HasStep());
  test_executor.Step();
  ASSERT_TRUE(result.load());

  thread_pool.Wait();
  thread_pool.SignalStop();
  thread_pool.WaitForStop();
}

TEST(Future, Async) {
  ThreadPool thread_pool(5);
  thread_pool.Start();

  auto f = Async(&thread_pool, []() { return Ok(5); });

  auto result = f.Get();

  ASSERT_TRUE(result.HasValue());
  ASSERT_EQ(5, result.Value());

  thread_pool.Wait();
  thread_pool.SignalStop();
  thread_pool.WaitForStop();
}

TEST(Future, Else) {
  ThreadPool thread_pool(5);
  thread_pool.Start();
  auto [f, p] = MakeContract<int32_t>();

  thread_pool.Execute(Lambda::Create([p = std::move(p)]() mutable {
    std::move(p).SetError(make_error_code(Error::internal));
  }));

  std::error_code error_code;
  auto result = f.Then([](int32_t value) { return Ok(value * 2); })
                    .Else([&error_code](std::error_code ec) -> Result<int32_t> {
                      error_code = ec;
                      return Err<int32_t>(ec);
                    })
                    .Get();

  ASSERT_TRUE(result.HasError());
  ASSERT_FALSE(result.HasValue());
  ASSERT_EQ(Error::internal, error_code);
  ASSERT_EQ(Error::internal, result.Error());

  thread_pool.Wait();
  thread_pool.SignalStop();
  thread_pool.WaitForStop();
}

TEST(Future, Revive) {
  ThreadPool thread_pool(5);
  thread_pool.Start();

  auto [f, p] = MakeContract<int32_t>();

  thread_pool.Execute(Lambda::Create([p = std::move(p)]() mutable {
    std::move(p).SetError(make_error_code(Error::internal));
  }));

  auto result = f.Then([](int32_t value) { return Ok(value * 2); })
                    .Else([](std::error_code /*ec*/) { return Ok(100); })
                    .Get();

  ASSERT_FALSE(result.HasError());
  ASSERT_TRUE(result.HasValue());
  ASSERT_EQ(100, result.Value());

  thread_pool.Wait();
  thread_pool.SignalStop();
  thread_pool.WaitForStop();
}

TEST(Future, FirstAllValues) {
  ThreadPool thread_pool(5);
  thread_pool.Start();

  auto [f1, p1] = MakeContract<int32_t>();
  auto [f2, p2] = MakeContract<int32_t>();
  auto [f3, p3] = MakeContract<int32_t>();

  thread_pool.Execute(Lambda::Create([p1 = std::move(p1)]() mutable {
    std::this_thread::sleep_for(500ms);
    std::move(p1).SetValue(3);
  }));
  thread_pool.Execute(Lambda::Create([p2 = std::move(p2)]() mutable {
    std::this_thread::sleep_for(300ms);
    std::move(p2).SetValue(2);
  }));
  thread_pool.Execute(Lambda::Create([p3 = std::move(p3)]() mutable {
    std::this_thread::sleep_for(100ms);
    std::move(p3).SetValue(1);
  }));

  std::vector<Future<int32_t>> futures;
  futures.push_back(std::move(f1));
  futures.push_back(std::move(f2));
  futures.push_back(std::move(f3));
  auto first = First(std::move(futures));

  auto result = first.Get();

  ASSERT_FALSE(result.HasError());
  ASSERT_TRUE(result.HasValue());
  ASSERT_EQ(1, result.Value());

  thread_pool.Wait();
  thread_pool.SignalStop();
  thread_pool.WaitForStop();
}

TEST(Future, FirstOneValue) {
  ThreadPool thread_pool(5);
  thread_pool.Start();

  auto [f1, p1] = MakeContract<int32_t>();
  auto [f2, p2] = MakeContract<int32_t>();
  auto [f3, p3] = MakeContract<int32_t>();

  thread_pool.Execute(Lambda::Create([p1 = std::move(p1)]() mutable {
    std::this_thread::sleep_for(300ms);
    std::move(p1).SetValue(3);
  }));
  thread_pool.Execute(Lambda::Create([p2 = std::move(p2)]() mutable {
    std::this_thread::sleep_for(200ms);
    std::move(p2).SetError(make_error_code(Error::internal));
  }));
  thread_pool.Execute(Lambda::Create([p3 = std::move(p3)]() mutable {
    std::this_thread::sleep_for(100ms);
    std::move(p3).SetError(make_error_code(Error::internal));
  }));

  std::vector<Future<int32_t>> futures;
  futures.push_back(std::move(f1));
  futures.push_back(std::move(f2));
  futures.push_back(std::move(f3));
  auto first = First(std::move(futures));

  auto result = first.Get();

  ASSERT_FALSE(result.HasError());
  ASSERT_TRUE(result.HasValue());
  ASSERT_EQ(3, result.Value());

  thread_pool.Wait();
  thread_pool.SignalStop();
  thread_pool.WaitForStop();
}

TEST(Future, FirstAllErrors) {
  ThreadPool thread_pool(5);
  thread_pool.Start();

  auto [f1, p1] = MakeContract<int32_t>();
  auto [f2, p2] = MakeContract<int32_t>();
  auto [f3, p3] = MakeContract<int32_t>();

  thread_pool.Execute(Lambda::Create([p1 = std::move(p1)]() mutable {
    std::this_thread::sleep_for(300ms);
    std::move(p1).SetError(make_error_code(Error::internal));
  }));
  thread_pool.Execute(Lambda::Create([p2 = std::move(p2)]() mutable {
    std::this_thread::sleep_for(200ms);
    std::move(p2).SetError(make_error_code(Error::internal));
  }));
  thread_pool.Execute(Lambda::Create([p3 = std::move(p3)]() mutable {
    std::this_thread::sleep_for(100ms);
    std::move(p3).SetError(make_error_code(Error::internal));
  }));

  std::vector<Future<int32_t>> futures;
  futures.push_back(std::move(f1));
  futures.push_back(std::move(f2));
  futures.push_back(std::move(f3));
  auto first = First(std::move(futures));

  auto result = first.Get();

  ASSERT_TRUE(result.HasError());
  ASSERT_FALSE(result.HasValue());
  ASSERT_EQ(Error::internal, result.Error());

  thread_pool.Wait();
  thread_pool.SignalStop();
  thread_pool.WaitForStop();
}

TEST(Future, AllValues) {
  ThreadPool thread_pool(5);
  thread_pool.Start();

  auto [f1, p1] = MakeContract<int32_t>();
  auto [f2, p2] = MakeContract<int32_t>();
  auto [f3, p3] = MakeContract<int32_t>();

  thread_pool.Execute(Lambda::Create([p1 = std::move(p1)]() mutable {
    std::this_thread::sleep_for(500ms);
    std::move(p1).SetValue(3);
  }));
  thread_pool.Execute(Lambda::Create([p2 = std::move(p2)]() mutable {
    std::this_thread::sleep_for(300ms);
    std::move(p2).SetValue(2);
  }));
  thread_pool.Execute(Lambda::Create([p3 = std::move(p3)]() mutable {
    std::this_thread::sleep_for(100ms);
    std::move(p3).SetValue(1);
  }));

  std::vector<Future<int32_t>> futures;
  futures.push_back(std::move(f1));
  futures.push_back(std::move(f2));
  futures.push_back(std::move(f3));
  auto all = All(std::move(futures));

  auto result = all.Get();

  ASSERT_FALSE(result.HasError());
  ASSERT_TRUE(result.HasValue());
  auto values = result.Value();
  ASSERT_EQ(3, values.size());
  ASSERT_EQ(1, values[0]);
  ASSERT_EQ(2, values[1]);
  ASSERT_EQ(3, values[2]);

  thread_pool.Wait();
  thread_pool.SignalStop();
  thread_pool.WaitForStop();
}

TEST(Future, AllOneError) {
  ThreadPool thread_pool(5);
  thread_pool.Start();

  auto [f1, p1] = MakeContract<int32_t>();
  auto [f2, p2] = MakeContract<int32_t>();
  auto [f3, p3] = MakeContract<int32_t>();

  thread_pool.Execute(Lambda::Create([p1 = std::move(p1)]() mutable {
    std::this_thread::sleep_for(500ms);
    std::move(p1).SetValue(3);
  }));
  thread_pool.Execute(Lambda::Create([p2 = std::move(p2)]() mutable {
    std::this_thread::sleep_for(300ms);
    std::move(p2).SetError(make_error_code(Error::internal));
  }));
  thread_pool.Execute(Lambda::Create([p3 = std::move(p3)]() mutable {
    std::this_thread::sleep_for(100ms);
    std::move(p3).SetValue(1);
  }));

  std::vector<Future<int32_t>> futures;
  futures.push_back(std::move(f1));
  futures.push_back(std::move(f2));
  futures.push_back(std::move(f3));
  auto all = All(std::move(futures));

  auto result = all.Get();

  ASSERT_TRUE(result.HasError());
  ASSERT_FALSE(result.HasValue());
  ASSERT_EQ(Error::internal, result.Error());

  thread_pool.Wait();
  thread_pool.SignalStop();
  thread_pool.WaitForStop();
}