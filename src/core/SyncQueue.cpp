#include "SyncQueue.h"
#include <spdlog/spdlog.h>
#include <cmath>

namespace sync {

    SyncQueue::SyncQueue(event::EventLoop& loop, DispatchFn dispatchFn)
        : loop_(loop), dispatch_(std::move(dispatchFn)) {}

    // push  — called from FileWatcher thread
    void SyncQueue::push(const FileEvent& incoming) {
        auto now = std::chrono::steady_clock::now();
        std::string key = incoming.path.string();

        std::lock_guard<std::mutex> lock(queue_mutex_);

        // Debounce: if same path was seen within the window, drop this event
        // (the earlier queued event already covers it)
        auto it = lastSeen_.find(key);
        if (it != lastSeen_.end()) {
            auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - it->second).count();
            if (age < kDebounceWindow.count()) {
                spdlog::info("[SyncQueue] Debounced '{}' ({}ms since last)", key, age);
                return;
            }
        }

        lastSeen_[key] = now;
        pq_.push(incoming);
        spdlog::info("[SyncQueue] Queued {} '{}' (queue size: {})",
                      toString(incoming.type), key, pq_.size());

        // Kick the drain loop if not already running
        if (!draining_) {
            draining_ = true;
            scheduleNext();
        }
    }

    // scheduleNext  — posts one item from pq_ into the EventLoop
    void SyncQueue::scheduleNext() {
        FileEvent next;
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if (pq_.empty()) {
                draining_ = false;
                return;
            }
            next = pq_.top();
            pq_.pop();
        }

        // Post to EventLoop — runs on its thread, not the watcher thread
        loop_.schedule({
            "sync:" + next.path.filename().string(),
            [this, ev = std::move(next)]() mutable {
                handleEvent(std::move(ev));
            }
        });
    }

    // handleEvent  — runs on EventLoop thread
    void SyncQueue::handleEvent(FileEvent event) {
        spdlog::info("[SyncQueue] Dispatching {} '{}' (attempt {})",
                     toString(event.type),
                     event.path.string(),
                     event.retryCount + 1);

        bool ok = dispatch_(event);

        if (ok) {
            spdlog::info("[SyncQueue] ✓ Synced '{}'", event.path.string());
            // Clean up debounce entry so future changes are accepted
            {
                std::lock_guard<std::mutex> lock(queue_mutex_);
                lastSeen_.erase(event.path.string());
            }
        } else {
            event.retryCount++;
            if (event.retryCount >= event.maxRetries) {
                spdlog::error("[SyncQueue] ✗ Giving up on '{}' after {} attempts — "
                              "will journal (not yet implemented)",
                              event.path.string(), event.retryCount);
                // TODO Phase 4: write to local SQLite journal for later replay
            } else {
                scheduleRetry(std::move(event));
                return; // don't call scheduleNext yet — retry will do it
            }
        }

        // Pull the next item from the queue
        scheduleNext();
    }

    // scheduleRetry  — exponential backoff, then re-queues the event
    void SyncQueue::scheduleRetry(FileEvent event) {
        // backoff = base * 2^retryCount, capped at max
        uint32_t backoffMs = std::min(
            static_cast<uint32_t>(kBackoffBaseMs * std::pow(2, event.retryCount - 1)),
            kMaxBackoffMs
        );

        spdlog::warn("[SyncQueue] Retry {}/{} for '{}' in {}ms",
                     event.retryCount, event.maxRetries,
                     event.path.string(), backoffMs);

        // Don't block the EventLoop thread. Spawn a detached waiter thread
        // that sleeps for the backoff duration and then re-queues the event.
        // This keeps the EventLoop responsive while still providing a simple
        // exponential backoff for retries. In a production system this would
        // be replaced by a timer wheel or dedicated timer queue.
        std::thread([this, ev = std::move(event), backoffMs]() mutable {
            try {
                std::this_thread::sleep_for(std::chrono::milliseconds(backoffMs));

                {
                    std::lock_guard<std::mutex> lock(queue_mutex_);
                    pq_.push(std::move(ev));
                }

                // Kick the drain loop from the watcher/retry thread
                scheduleNext();
            } catch (const std::exception& e) {
                spdlog::error("[SyncQueue] Retry thread threw: {}", e.what());
            }
        }).detach();
    }

    // flush  — drain everything on shutdown
    void SyncQueue::flush() {
        spdlog::info("[SyncQueue] Flushing {} pending events...", pendingCount());
        // scheduleNext will keep chaining until the queue is empty
        if (!draining_) {
            draining_ = true;
            scheduleNext();
        }
    }

    size_t SyncQueue::pendingCount() const {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        return pq_.size();
    }

} // namespace sync