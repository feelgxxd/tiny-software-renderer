#pragma once
// Persistent worker pool with a single blocking primitive: parallelFor.
// Work items are claimed from a shared atomic counter, so uneven items
// (e.g. screen tiles with very different triangle counts) balance naturally.

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace sr {

class ThreadPool {
public:
    // threadCount <= 0 picks std::thread::hardware_concurrency().
    explicit ThreadPool(int threadCount = 0);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // Total threads that execute work, including the calling thread.
    int threadCount() const { return static_cast<int>(m_workers.size()) + 1; }

    // Calls fn(itemIndex, workerIndex) for every item in [0, itemCount).
    // workerIndex is in [0, threadCount()) and is stable for the duration of
    // one call, so it can index per-thread scratch memory. Blocks until done.
    void parallelFor(int itemCount, const std::function<void(int item, int worker)>& fn);

    static int hardwareThreads();

private:
    void workerLoop(int workerIndex);
    void runItems(int workerIndex);

    std::vector<std::thread> m_workers;

    std::mutex m_mutex;
    std::condition_variable m_wake;
    std::condition_variable m_done;

    const std::function<void(int, int)>* m_job = nullptr;
    int m_itemCount = 0;
    std::atomic<int> m_nextItem{0};
    int m_activeWorkers = 0;
    uint64_t m_generation = 0;
    bool m_stop = false;
};

} // namespace sr
