#include "core/jobs.h"
#include "core/util.h"

namespace vx {

JobSystem::JobSystem(int workers) {
    if (workers <= 0) { int hc = (int)std::thread::hardware_concurrency(); workers = std::max(1, hc - 1); }
    for (int i = 0; i < workers; i++) threads_.emplace_back([this] { workerLoop(); });
}

JobSystem::~JobSystem() {
    { std::lock_guard<std::mutex> lk(m_); stop_ = true; for (auto& j : queue_) if (j.cancel) j.cancel->store(true); }
    cv_.notify_all();
    for (auto& t : threads_) t.join();
}

void JobSystem::submit(std::function<void()> work, std::function<float()> priority, CancelToken cancel) {
    { std::lock_guard<std::mutex> lk(m_); queue_.push_back({std::move(work), std::move(priority), std::move(cancel), seq_++}); }
    cv_.notify_one();
}

void JobSystem::postMain(std::function<void()> fn) {
    std::lock_guard<std::mutex> lk(mainM_); main_.push_back(std::move(fn));
}

int JobSystem::pumpMain(double budgetMs) {
    double t0 = nowSeconds(); int n = 0;
    for (;;) {
        std::function<void()> fn;
        { std::lock_guard<std::mutex> lk(mainM_); if (main_.empty()) break; fn = std::move(main_.front()); main_.pop_front(); }
        fn(); n++;
        if (budgetMs > 0 && (nowSeconds() - t0) * 1000.0 > budgetMs) break;
    }
    return n;
}

void JobSystem::workerLoop() {
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lk(m_);
            cv_.wait(lk, [&] { return stop_ || !queue_.empty(); });
            if (stop_ && queue_.empty()) return;
            // pick best priority; drop cancelled jobs while scanning
            size_t best = SIZE_MAX; float bestP = 0;
            for (size_t i = 0; i < queue_.size();) {
                if (queue_[i].cancel && queue_[i].cancel->load()) { queue_[i] = std::move(queue_.back()); queue_.pop_back(); cancelled_++; continue; }
                float p = queue_[i].prio ? queue_[i].prio() : 0.0f;
                if (best == SIZE_MAX || p < bestP || (p == bestP && queue_[i].seq < queue_[best].seq)) { best = i; bestP = p; }
                i++;
            }
            if (best == SIZE_MAX) { if (running_ == 0) idleCv_.notify_all(); continue; }
            job = std::move(queue_[best]); queue_[best] = std::move(queue_.back()); queue_.pop_back();
            running_++;
        }
        bool skip = job.cancel && job.cancel->load();
        if (!skip) job.work();
        {
            std::lock_guard<std::mutex> lk(m_);
            running_--; if (skip) cancelled_++; else completed_++;
            if (running_ == 0 && queue_.empty()) idleCv_.notify_all();
        }
    }
}

void JobSystem::waitIdle() {
    std::unique_lock<std::mutex> lk(m_);
    idleCv_.wait(lk, [&] { return queue_.empty() && running_ == 0; });
}

JobSystem::Stats JobSystem::stats() const {
    std::lock_guard<std::mutex> lk(m_);
    return {(int)queue_.size(), running_, completed_, cancelled_, (int)threads_.size()};
}

} // namespace vx
