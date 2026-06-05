#include "core/AgentConfig.h"
#include "core/EventLoop.h"
#include "core/SyncQueue.h"
#include "filesystem/IFileWatcher.h"
#include <spdlog/spdlog.h>
#include <filesystem>
#include <csignal>

// Global so the signal handler can reach them
event::EventLoop*    g_loop    = nullptr;
watcher::IFileWatcher* g_watcher = nullptr;

void onSignal(int) {
    spdlog::info("Shutdown signal received.");
    if (g_watcher) g_watcher->stop();
    if (g_loop)    g_loop->stop();
}

int main(int argc, char* argv[]) {
    spdlog::set_level(spdlog::level::debug);
    std::signal(SIGINT,  onSignal);
    std::signal(SIGTERM, onSignal);

    std::filesystem::path cfgPath = (argc > 1)
        ? argv[1]
        : std::filesystem::current_path() / "agent.toml";

    spdlog::info("AnyWhereDoor agent starting...");

    awd::AgentConfig config;
    try {
        config = awd::AgentConfigLoader::loadFromFile(cfgPath);
        awd::AgentConfigLoader::saveToFile(config, cfgPath);
    } catch (const std::exception& e) {
        spdlog::critical("Config error: {}", e.what());
        return 1;
    }

    spdlog::info("Agent ID : {}", config.agentId);
    spdlog::info("Watching : {} path(s)", config.watchEntries.size());
    for (auto& w : config.watchEntries)
        spdlog::info("  -> {}", w.path.string());

    event::EventLoop loop;
    g_loop = &loop;

    sync::DispatchFn stubDispatch = [](const sync::FileEvent& ev) -> bool {
        spdlog::info("[Dispatch] {} '{}'",
                     sync::toString(ev.type), ev.path.string());
        return true;
    };

    sync::SyncQueue syncQueue(loop, stubDispatch);

    auto fileWatcher = watcher::IFileWatcher::create(config.watchEntries, syncQueue);
    g_watcher = fileWatcher.get();

    try {
        fileWatcher->start();
        spdlog::info("Watching for file changes. Press Ctrl+C to stop.");
        loop.wait();
    } catch (const std::exception& e) {
        spdlog::critical("Fatal: {}", e.what());
        return 1;
    }

    spdlog::info("Agent shut down cleanly.");
    return 0;
}