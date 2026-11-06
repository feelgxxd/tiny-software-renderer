#include "core/thread_pool.h"

#include <algorithm>

namespace sr {

int ThreadPool::hardwareThreads()
{
    unsigned n = std::thread::hardware_concurrency();
    return n == 0 ? 1 : static_cast<int>(n);
}

ThreadPool::ThreadPool(int threadCount)
{
    if (threadCount <= 0) threadCount = hardwareThreads();
    threadCount = std::max(1, threadCount);

    // The calling thread participates, so spawn threadCount - 1 workers.
    for (int i = 1; i < threadCount; ++i)
        m_workers.emplace_back([this, i] { workerLoop(i); });
    m_activeThreads = threadCount;
}

void ThreadPool::setActiveThreads(int count)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_activeThreads = std::clamp(count, 1, capacity());
}

ThreadPool::~ThreadPool()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = true;
    }
    m_wake.notify_all();
    for (auto& t : m_workers) t.join();
}

void ThreadPool::runItems(int workerIndex)
{
    for (;;) {
        int item = m_nextItem.fetch_add(1, std::memory_order_relaxed);
        if (item >= m_itemCount) break;
        (*m_job)(item, workerIndex);
    }
}

void ThreadPool::workerLoop(int workerIndex)
{
    uint64_t seenGeneration = 0;
    for (;;) {
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_wake.wait(lock, [&] { return m_stop || m_generation != seenGeneration; });
            if (m_stop) return;
            seenGeneration = m_generation;
            // Workers beyond the active count sit this job out (and aren't waited for).
            if (workerIndex >= m_jobThreads) continue;
        }

        runItems(workerIndex);

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (--m_activeWorkers == 0) m_done.notify_one();
        }
    }
}

void ThreadPool::parallelFor(int itemCount, const std::function<void(int, int)>& fn)
{
    if (itemCount <= 0) return;

    int threads;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        threads = m_activeThreads;
    }
    if (threads <= 1 || itemCount == 1) {
        for (int i = 0; i < itemCount; ++i) fn(i, 0);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_job = &fn;
        m_itemCount = itemCount;
        m_nextItem.store(0, std::memory_order_relaxed);
        m_jobThreads = threads;
        m_activeWorkers = threads - 1;
        ++m_generation;
    }
    m_wake.notify_all();

    runItems(0);

    std::unique_lock<std::mutex> lock(m_mutex);
    m_done.wait(lock, [&] { return m_activeWorkers == 0; });
    m_job = nullptr;
}

} // namespace sr
