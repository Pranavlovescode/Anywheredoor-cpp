//
// Created by deilsy on 04-06-2026.
//

#include "IFileWatcher.h"
#include "WinWatcher.h"
#include <spdlog/spdlog.h>
#include <chrono>

namespace watcher {


    std::unique_ptr<IFileWatcher> IFileWatcher::create(
        const std::vector<awd::WatchEntry>& watches,
        sync::SyncQueue& queue)
    {
        return std::make_unique<WinWatcher>(watches, queue);
    }

    WinWatcher::WinWatcher(const std::vector<awd::WatchEntry>& watches,
                           sync::SyncQueue& queue)
        : queue_(queue), entries_(watches)
    {
        stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (stopEvent_ == INVALID_HANDLE_VALUE)
            throw std::runtime_error("[WinWatcher] Failed to create stop event.");
    }

    WinWatcher::~WinWatcher() {
        stop();
        if (stopEvent_ != INVALID_HANDLE_VALUE)
            CloseHandle(stopEvent_);
    }


    void WinWatcher::start() {
        running_ = true;

        // Open a directory handle + arm ReadDirectoryChangesW for each watch entry
        for (auto& entry : entries_) {
            WatchHandle wh;
            wh.entry = entry;

            wh.dirHandle = CreateFileW(
                entry.path.wstring().c_str(),
                FILE_LIST_DIRECTORY,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr,
                OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
                nullptr
            );

            if (wh.dirHandle == INVALID_HANDLE_VALUE) {
                spdlog::error("[WinWatcher] Cannot open directory '{}': error {}",
                              entry.path.string(), GetLastError());
                continue;
            }

            wh.overlapped.hEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);

            if (!armWatch(wh)) {
                CloseHandle(wh.dirHandle);
                continue;
            }

            spdlog::info("[WinWatcher] Watching '{}'", entry.path.string());
            handles_.push_back(std::move(wh));
        }

        if (handles_.empty())
            throw std::runtime_error("[WinWatcher] No valid watch handles — nothing to watch.");

        thread_ = std::thread(&WinWatcher::watchLoop, this);
    }


    bool WinWatcher::armWatch(WatchHandle& wh) {
        DWORD filter =
            FILE_NOTIFY_CHANGE_FILE_NAME  |   // create / delete / rename
            FILE_NOTIFY_CHANGE_DIR_NAME   |
            FILE_NOTIFY_CHANGE_LAST_WRITE |   // modify
            FILE_NOTIFY_CHANGE_SIZE;

        BOOL ok = ReadDirectoryChangesW(
            wh.dirHandle,
            wh.buffer,
            sizeof(wh.buffer),
            wh.entry.recursive ? TRUE : FALSE,
            filter,
            nullptr,            // lpBytesReturned — unused in async mode
            &wh.overlapped,
            nullptr             // no completion routine — we use IOCP/events
        );

        if (!ok) {
            spdlog::error("[WinWatcher] ReadDirectoryChangesW failed for '{}': error {}",
                          wh.entry.path.string(), GetLastError());
            return false;
        }
        return true;
    }

    void WinWatcher::watchLoop() {
        // Build a wait-list: one event per handle + the stop event
        std::vector<HANDLE> waitHandles;
        for (auto& wh : handles_)
            waitHandles.push_back(wh.overlapped.hEvent);
        waitHandles.push_back(stopEvent_);  // last index = stop signal

        const DWORD stopIdx = static_cast<DWORD>(waitHandles.size() - 1);

        while (running_) {
            DWORD result = WaitForMultipleObjects(
                static_cast<DWORD>(waitHandles.size()),
                waitHandles.data(),
                FALSE,       // wait for ANY one handle
                INFINITE
            );

            if (result == WAIT_FAILED) {
                spdlog::error("[WinWatcher] WaitForMultipleObjects failed: {}", GetLastError());
                break;
            }

            DWORD idx = result - WAIT_OBJECT_0;

            if (idx == stopIdx) {
                spdlog::info("[WinWatcher] Stop signal received.");
                break;
            }

            if (idx < handles_.size()) {
                processNotifications(handles_[idx]);
                // Re-arm so we keep receiving future changes
                armWatch(handles_[idx]);
            }
        }

        spdlog::info("[WinWatcher] Watch loop exited.");
    }


    void WinWatcher::processNotifications(WatchHandle& wh) {
        DWORD bytesTransferred = 0;
        GetOverlappedResult(wh.dirHandle, &wh.overlapped, &bytesTransferred, FALSE);

        if (bytesTransferred == 0) return;

        auto* info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(wh.buffer);

        // We need to remember the previous entry for rename pairs
        std::wstring pendingRenamePath;

        while (true) {
            // Convert wide filename to std::filesystem::path
            std::wstring wname(info->FileName, info->FileNameLength / sizeof(wchar_t));
            std::filesystem::path fullPath = wh.entry.path / wname;

            // --- Ignore filter ---
            bool ignored = false;
            for (auto& pattern : wh.entry.ignorePatterns) {
                // Simple suffix / segment match (full glob needs a library)
                if (fullPath.string().find(pattern) != std::string::npos) {
                    ignored = true;
                    break;
                }
            }

            if (!ignored) {
                sync::FileEvent ev;
                ev.detectedAt = std::chrono::steady_clock::now();
                ev.path       = fullPath;

                switch (info->Action) {
                    case FILE_ACTION_ADDED:
                        ev.type = sync::FileTypeEvent::created;
                        queue_.push(ev);
                        spdlog::info("[WinWatcher] CREATED  {}", fullPath.string());
                        break;

                    case FILE_ACTION_REMOVED:
                        ev.type = sync::FileTypeEvent::deleted;
                        queue_.push(ev);
                        spdlog::info("[WinWatcher] DELETED  {}", fullPath.string());
                        break;

                    case FILE_ACTION_MODIFIED:
                        ev.type = sync::FileTypeEvent::modified;
                        queue_.push(ev);
                        spdlog::info("[WinWatcher] MODIFIED {}", fullPath.string());
                        break;

                    case FILE_ACTION_RENAMED_OLD_NAME:
                        // Windows sends OLD then NEW as consecutive entries
                        pendingRenamePath = wname;
                        break;

                    case FILE_ACTION_RENAMED_NEW_NAME:
                        ev.type    = sync::FileTypeEvent::renamed;
                        ev.old_path = wh.entry.path / pendingRenamePath;
                        queue_.push(ev);
                        spdlog::info("[WinWatcher] RENAMED  {} -> {}",
                                      ev.old_path.string(), fullPath.string());
                        pendingRenamePath.clear();
                        break;
                }
            }

            if (info->NextEntryOffset == 0) break;
            info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(
                reinterpret_cast<char*>(info) + info->NextEntryOffset);
        }
    }

    void WinWatcher::stop() {
        if (!running_) return;
        running_ = false;
        SetEvent(stopEvent_);   // unblocks WaitForMultipleObjects
        if (thread_.joinable()) thread_.join();

        for (auto& wh : handles_) {
            CloseHandle(wh.overlapped.hEvent);
            CloseHandle(wh.dirHandle);
        }
        handles_.clear();
    }
}