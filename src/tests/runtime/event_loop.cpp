#include <chrono>
#include <fcntl.h>
#include <gtest/gtest.h>
#include <queue>
#include <thread>
#include <unistd.h>

#include "runtime/api.hpp"
#include "runtime/event_loop.hpp"
#include "runtime/task.hpp"
#include "runtime/thread_pool.hpp"
#include "util/syscall.hpp"

using namespace std::chrono_literals;

TEST(EventLoop, SmokeTest) {
  EventLoop event_loop(100);

  event_loop.Setup();

  std::thread loop_thread([&]() { event_loop.Run(); });

  event_loop.Stop();
  loop_thread.join();
}

TEST(EventLoop, MultipleRun) {
  const size_t num_loops = 5;

  EventLoop event_loop(100);
  event_loop.Setup();

  ThreadPool thread_pool(num_loops);
  thread_pool.Start();

  for (size_t i = 0; i < num_loops; ++i) {
    thread_pool.Execute(Lambda::Create([&event_loop]() { event_loop.Run(); }));
  }

  std::this_thread::sleep_for(100ms);
  event_loop.Stop();

  thread_pool.Wait();
  thread_pool.SignalStop();
  thread_pool.WaitForStop();
}

class Pipe {
public:
  using Message = int32_t;

  Pipe() {
    int pipefd[2];
    CANNOT_FAIL(pipe2(pipefd, O_DIRECT), "pipe failed");
    read_fd_ = pipefd[0];
    write_fd_ = pipefd[1];
  }

  Message Read() {
    Message buffer;
    auto read_result =
        read(ReadFd(), reinterpret_cast<void *>(&buffer), sizeof(Message));
    CANNOT_FAIL(read_result, "read from pipe failed");
    return buffer;
  }

  void Write(Message msg) {
    auto write_result =
        write(WriteFd(), reinterpret_cast<void *>(&msg), sizeof(Message));
    CANNOT_FAIL(write_result, "write from pipe failed");
  }

  int ReadFd() const { return read_fd_; }

  int WriteFd() const { return write_fd_; }

private:
  int read_fd_;
  int write_fd_;
};

class MessageHandler : public IHandler {
public:
  MessageHandler(Pipe &pipe) : pipe_(pipe) {}

  void Handle(EventType type, EventLoop &event_loop) final {
    message_.store(pipe_.Read());

    Event event{pipe_.ReadFd(), this, type};
    event_loop.ModEvent(event);
  }

  Pipe::Message GetMessage() { return message_.load(); }

private:
  Pipe &pipe_;

  std::atomic<Pipe::Message> message_;
};

TEST(EventLoop, SimplePipe) {
  EventLoop event_loop(100);
  event_loop.Setup();

  ThreadPool thread_pool(5);
  thread_pool.Start();

  thread_pool.Execute(Lambda::Create([&event_loop]() { event_loop.Run(); }));

  Pipe pipe;
  MessageHandler handler(pipe);
  Event pipe_event{pipe.ReadFd(), &handler, EventType::kReadReady};
  event_loop.AddEvent(pipe_event);

  std::this_thread::sleep_for(50ms);
  pipe.Write(1);
  std::this_thread::sleep_for(50ms);
  ASSERT_EQ(1, handler.GetMessage());
  pipe.Write(2);
  std::this_thread::sleep_for(50ms);
  ASSERT_EQ(2, handler.GetMessage());
  pipe.Write(3);
  std::this_thread::sleep_for(50ms);
  ASSERT_EQ(3, handler.GetMessage());

  event_loop.Stop();

  thread_pool.Wait();
  thread_pool.SignalStop();
  thread_pool.WaitForStop();
}

class SumMessageHandler : public IHandler {
public:
  SumMessageHandler(Pipe &pipe) : pipe_(pipe) {}

  void Handle(EventType type, EventLoop &event_loop) final {
    auto msg = pipe_.Read();

    Event event{pipe_.ReadFd(), this, type};
    event_loop.ModEvent(event);

    std::this_thread::sleep_for(10ms);
    counter_.fetch_add(msg);
  }

  int32_t GetCounter() { return counter_.load(); }

private:
  Pipe &pipe_;

  std::atomic<int32_t> counter_{0};
};

TEST(EventLoop, Pipe) {
  EventLoop event_loop(100);
  event_loop.Setup();

  ThreadPool thread_pool(5);
  thread_pool.Start();

  constexpr size_t num_loops = 5;
  for (size_t i = 0; i < num_loops; ++i) {
    thread_pool.Execute(Lambda::Create([&event_loop]() { event_loop.Run(); }));
  }

  Pipe pipe;
  SumMessageHandler handler(pipe);
  Event pipe_event{pipe.ReadFd(), &handler, EventType::kReadReady};
  event_loop.AddEvent(pipe_event);

  constexpr size_t num_writes = 100;
  for (size_t i = 0; i < num_writes; ++i) {
    pipe.Write(1);
  }

  std::this_thread::sleep_for(300ms);
  auto counter = handler.GetCounter();
  ASSERT_EQ(num_writes, counter);

  event_loop.Stop();

  thread_pool.Wait();
  thread_pool.SignalStop();
  thread_pool.WaitForStop();
}

class FiberMessageHandler : public IHandler {
public:
  FiberMessageHandler(Pipe &pipe) : pipe_(pipe) {}

  void Handle(EventType type, EventLoop &event_loop) final {
    auto msg = pipe_.Read();

    Event event{pipe_.ReadFd(), this, type};
    event_loop.ModEvent(event);

    Go([this, msg]() noexcept { counter_.fetch_add(msg); });
  }

  int32_t GetCounter() { return counter_.load(); }

private:
  Pipe &pipe_;

  std::atomic<int32_t> counter_{0};
};

TEST(EventLoop, Fibers) {
  EventLoop event_loop(100);
  event_loop.Setup();

  ThreadPool thread_pool(5);
  thread_pool.Start();

  constexpr size_t num_loops = 3;
  for (size_t i = 0; i < num_loops; ++i) {
    Go(&thread_pool, [&event_loop]() noexcept { event_loop.Run(); });
  }

  Pipe pipe;
  FiberMessageHandler handler(pipe);
  Event pipe_event{pipe.ReadFd(), &handler, EventType::kReadReady};
  event_loop.AddEvent(pipe_event);

  constexpr size_t num_writes = 100;
  for (size_t i = 0; i < num_writes; ++i) {
    pipe.Write(1);
  }

  std::this_thread::sleep_for(300ms);
  auto counter = handler.GetCounter();
  ASSERT_EQ(num_writes, counter);

  event_loop.Stop();

  thread_pool.Wait();
  thread_pool.SignalStop();
  thread_pool.WaitForStop();
}
