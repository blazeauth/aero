#include <gtest/gtest.h>

#include <algorithm>

#include "aero/io_runtime.hpp"

namespace {
  using aero::io_runtime;
}

TEST(IoRuntime, SpawnsNumThreadsGivenInConstructor) {
  using thread_id = io_runtime::thread_id;

  std::mutex mutex;
  std::vector<thread_id> thread_ids;

  auto on_thread_init = [&mutex, &thread_ids](thread_id id) {
    std::scoped_lock lock(mutex);

    auto is_thread_id_logged = std::ranges::contains(thread_ids, id);
    if (!is_thread_id_logged) {
      thread_ids.push_back(id);
    }
  };

  io_runtime runtime(aero::threads_count_t{4}, on_thread_init, aero::wait_threads);

  EXPECT_EQ(runtime.threads_count(), thread_ids.size());
}
