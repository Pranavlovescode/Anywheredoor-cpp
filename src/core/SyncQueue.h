//
// Created by deilsy on 05-06-2026.
//

#ifndef ANYWHEREDOOR_SYNCQUEUE_H
#define ANYWHEREDOOR_SYNCQUEUE_H
#include <filesystem>
#include <functional>
#include <string>

#include "EventLoop.h"

namespace sync {
    enum class FileTypeEvent {
        created = 0,
        modified = 1,
        renamed = 2,
        deleted = 3,  // highest priority - must reach server faster
    };

    inline std::string toString(FileTypeEvent evt) {
        switch (evt) {
            case FileTypeEvent::created: return "CREATED";
            case FileTypeEvent::modified: return "MODIFIED";
            case FileTypeEvent::renamed: return "RENAMED";
            case FileTypeEvent::deleted: return "DELETED";
            default: return "UNKNOWN";
        }
    }

    struct FileEvent {
        std::filesystem::path path;
        std::filesystem::path old_path;
        FileTypeEvent type;
        std::chrono::steady_clock::time_point detectedAt;

        // retry tracking
        uint32_t retryCount{0};
        uint32_t maxRetries{5};

        int priority() const{
            return static_cast<int>(type);
        }
    };

    struct compartor {
        bool operator() (const FileEvent&a , const FileEvent&b) const {
            if (a.priority() != b.priority()) return a.priority() < b.priority();
            return a.detectedAt > b.detectedAt;
        }
    };


    using DispatchFn = std::function<bool(const FileEvent&)>;

    class SyncQueue {
    public:
        explicit SyncQueue(event::EventLoop& loop, DispatchFn dispatch);

        void push(const FileEvent& evt);
        void flush();
        size_t pendingCount() const;

    private:
        void scheduleNext();
        void handleEvent(FileEvent event);
        void scheduleRetry(FileEvent event);

        event::EventLoop& loop_;
        DispatchFn dispatch_;

        mutable std::mutex queue_mutex_;
        std::priority_queue<
            FileEvent,
            std::vector<FileEvent>,
            compartor
        > pq_;

        // Dedup: track last-seen time per path to debounce rapid writes
        std::unordered_map<std::string, std::chrono::steady_clock::time_point> lastSeen_;
        // Debounce window — rapid writes to same file within this window
        // are collapsed into one event
        static constexpr auto kDebounceWindow = std::chrono::milliseconds(300);

        // Backoff config (mirrors TransportConfig — can be wired from AgentConfig)
        static constexpr uint32_t kBackoffBaseMs{500};
        static constexpr uint32_t kMaxBackoffMs{30000};

        std::atomic<bool> draining_{false}; // true when scheduleNext is in-flight


    };
} // sync

#endif //ANYWHEREDOOR_SYNCQUEUE_H
