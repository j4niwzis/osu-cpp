export module client.loader;

import std;

export namespace client {

// Background worker for anything that would otherwise stall a frame:
// unzipping and parsing beatmap archives, decoding audio, decoding cover art.
//
// Requests carry a job to run off-thread and a completion to run on the UI
// thread, mirroring the arrangement client.http already uses for network
// callbacks. Results are delivered in poll(), so nothing touches Skia or
// OpenAL from the worker.
class Loader {
public:
  using Job = std::function<void()>;          // runs on the worker
  using Completion = std::function<void()>;   // runs on the caller of poll()

  Loader() {
    fThread = std::thread([this] { this->run(); });
  }

  ~Loader() {
    {
      const std::scoped_lock lock(fMutex);
      fQuit = true;
    }
    fCv.notify_all();
    if (fThread.joinable()) {
      fThread.join();
    }
  }

  Loader(const Loader &) = delete;
  Loader &operator=(const Loader &) = delete;

  // Queue work. `key` deduplicates: queueing the same key twice while the
  // first is still pending is a no-op, which keeps fast scrolling from
  // stacking up dozens of identical loads.
  void submit(std::uint64_t key, Job job, Completion done) {
    {
      const std::scoped_lock lock(fMutex);
      if (!fPending.insert(key).second) {
        return;
      }
      fQueue.push_back({key, std::move(job), std::move(done)});
    }
    fCv.notify_one();
  }

  [[nodiscard]] bool busy() const {
    const std::scoped_lock lock(fMutex);
    return !fQueue.empty() || fRunning;
  }

  // Run finished completions. Call once per frame.
  // Returns how many completions ran, so a caller that only draws on demand
  // knows something arrived.
  std::size_t poll() {
    std::vector<Task> ready;
    {
      const std::scoped_lock lock(fMutex);
      ready.swap(fDone);
    }
    const std::size_t count = ready.size();
    for (auto &t : ready) {
      if (t.fDone) {
        t.fDone();
      }
      const std::scoped_lock lock(fMutex);
      fPending.erase(t.fKey);
    }
    return count;
  }

private:
  struct Task {
    std::uint64_t fKey = 0;
    Job fJob;
    Completion fDone;
  };

  void run() {
    for (;;) {
      Task task;
      {
        std::unique_lock lock(fMutex);
        fCv.wait(lock, [this] { return fQuit || !fQueue.empty(); });
        if (fQuit) {
          return;
        }
        task = std::move(fQueue.front());
        fQueue.pop_front();
        fRunning = true;
      }
      if (task.fJob) {
        try {
          task.fJob();
        } catch (const std::exception &e) {
          std::println(std::cerr, "[loader] job failed: {}", e.what());
        }
      }
      {
        const std::scoped_lock lock(fMutex);
        fRunning = false;
        fDone.push_back(std::move(task));
      }
    }
  }

  mutable std::mutex fMutex;
  std::condition_variable fCv;
  std::deque<Task> fQueue;
  std::vector<Task> fDone;
  std::set<std::uint64_t> fPending;
  bool fQuit = false;
  bool fRunning = false;
  std::thread fThread;
};

} // namespace client
