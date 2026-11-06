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

    if (m_workers.empty() || itemCount == 1) {
        for (int i = 0; i < itemCount; ++i) fn(i, 0);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_job = &fn;
        m_itemCount = itemCount;
        m_nextItem.store(0, std::memory_order_relaxed);
        m_activeWorkers = static_cast<int>(m_workers.size());
        ++m_generation;
    }
    m_wake.notify_all();

    runItems(0);

    std::unique_lock<std::mutex> lock(m_mutex);
    m_done.wait(lock, [&] { return m_activeWorkers == 0; });
    m_job = nullptr;
}

} // namespace sr
