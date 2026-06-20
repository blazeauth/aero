#include <algorithm>
#include <mutex>
#include <vector>

#include "ut.hpp"

#include "aero/io_runtime.hpp"

ut::suite io_runtime = [] {
  "spawns the number of threads given in constructor"_test = [] {
    using thread_id = aero::io_runtime::thread_id;

    std::mutex mutex;
    std::vector<thread_id> thread_ids;

    auto on_thread_init = [&mutex, &thread_ids](thread_id id) {
      std::scoped_lock lock(mutex);

      auto is_thread_id_logged = std::ranges::contains(thread_ids, id);
      if (!is_thread_id_logged) {
        thread_ids.push_back(id);
      }
    };

    aero::io_runtime runtime(4, on_thread_init, aero::wait_threads);

    expect(runtime.threads_count() == thread_ids.size());
  };
};

int main() {}
