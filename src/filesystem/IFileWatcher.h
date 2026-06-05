#ifndef ANYWHEREDOOR_IFILEWATCHER_H
#define ANYWHEREDOOR_IFILEWATCHER_H

#include "../core/AgentConfig.h"
#include <memory>
#include "../core/SyncQueue.h"

namespace watcher {

    class IFileWatcher {
    public:
        virtual ~IFileWatcher() = default;
        virtual void start() = 0;   // begins watching, non-blocking
        virtual void stop()  = 0;   // signals the watch thread to exit

        // Factory — returns the right impl for the current platform
        static std::unique_ptr<IFileWatcher> create(
            const std::vector<awd::WatchEntry>& watches,
            sync::SyncQueue& queue
        );
    };

} // namespace watcher

#endif