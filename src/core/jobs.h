// Job system: fixed worker pool, dynamic priorities, cancellation, main-thread result delivery.
//
// Priorities are re-evaluated when a worker picks the next job, so jobs queued while the player
// was elsewhere are automatically re-ranked by current relevance (distance to the camera).
// The queue is intentionally a flat vector scanned on pop: queue length is bounded by the chunk
// manager (a few hundred entries), so this is cheaper and simpler than a re-heapified queue.
#pragma once
#include "core/common.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace vx {

using CancelToken = std::shared_ptr<std::atomic<bool>>;
inline CancelToken makeCancelToken() { return std::make_shared<std::atomic<bool>>(false); }

class JobSystem {
public:
    struct Stats { int queued = 0; int running = 0; u64 completed = 0; u64 cancelled = 0; int workers = 0; };

    explicit JobSystem(int workers = 0);   // 0 = hardware_concurrency - 1 (min 1)
    ~JobSystem();
    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    // `priority` is called (under the queue lock) when choosing the next job; lower runs first.
    void submit(std::function<void()> work, std::function<float()> priority = nullptr, CancelToken cancel = nullptr);
    // Queue a closure to run on the main thread during the next pump().
    void postMain(std::function<void()> fn);
    // Run queued main-thread closures; stops early when `budgetMs` is exceeded (0 = unlimited).
    int pumpMain(double budgetMs = 0);
    // Block until queue empty and all workers idle (tests, shutdown, synchronous saves).
    void waitIdle();
    Stats stats() const;
    int workerCount() const { return (int)threads_.size(); }

private:
    struct Job { std::function<void()> work; std::function<float()> prio; CancelToken cancel; u64 seq; };
    void workerLoop();
    std::vector<std::thread> threads_;
    mutable std::mutex m_;
    std::condition_variable cv_, idleCv_;
    std::vector<Job> queue_;
    bool stop_ = false;
    int running_ = 0;
    u64 seq_ = 0, completed_ = 0, cancelled_ = 0;
    std::mutex mainM_;
    std::deque<std::function<void()>> main_;
};

} // namespace vx
