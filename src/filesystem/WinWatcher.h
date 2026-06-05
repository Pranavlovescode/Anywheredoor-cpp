//
// Created by deilsy on 05-06-2026.
//


#ifndef ANYWHEREDOOR_WINWATCHER_H
#define ANYWHEREDOOR_WINWATCHER_H

#include "IFileWatcher.h"
#include <windows.h>
#include <thread>
#include <atomic>
#include <vector>

namespace watcher {

    class WinWatcher : public IFileWatcher {
    public:
        WinWatcher(const std::vector<awd::WatchEntry>& watches,
                   sync::SyncQueue& queue);
        ~WinWatcher() override;

        void start() override;
        void stop()  override;

    private:
        struct WatchHandle {
            awd::WatchEntry     entry;
            HANDLE              dirHandle{INVALID_HANDLE_VALUE};
            OVERLAPPED          overlapped{};
            alignas(DWORD)
            char                buffer[64 * 1024]{};  // 64 KiB notify buffer
        };

        void watchLoop();
        void processNotifications(WatchHandle& wh);
        bool armWatch(WatchHandle& wh);

        sync::SyncQueue&                  queue_;
        std::vector<awd::WatchEntry>      entries_;
        std::vector<WatchHandle>          handles_;
        std::thread                       thread_;
        std::atomic<bool>                 running_{false};
        HANDLE                            stopEvent_{INVALID_HANDLE_VALUE};
    };

} // namespace watcher

#endif //ANYWHEREDOOR_WINWATCHER_H
