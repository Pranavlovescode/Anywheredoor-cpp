#include "core/AgentConfig.h"
#include "core/EventLoop.h"
#include <spdlog/spdlog.h>
#include <filesystem>
#include <csignal>
#include <atomic>

// Global event loop instance (for signal handler access)
event::EventLoop* g_eventLoop = nullptr;
std::atomic<bool> g_shutdownRequested(false);

// Signal handler for graceful shutdown
// void signalHandler(int signum) {
//     spdlog::info("Received signal {} - initiating graceful shutdown...", signum);
//     g_shutdownRequested.store(true);
//
//     if (g_eventLoop && g_eventLoop->isRunning()) {
//         // Post a shutdown event to wake up the event loop
//         auto shutdownEvent = std::make_shared<event::Event>(
//             event::EventType::SHUTDOWN,
//             event::EventPriority::CRITICAL
//         );
//         shutdownEvent->sourceId = "signal_handler";
//         g_eventLoop->postEvent(shutdownEvent);
//     }
// }

int main(int argc, char* argv[]) {

    std::filesystem::path cfgPath;

    if (argc > 1) {
        cfgPath = argv[1];
    } else {
        // Default: same directory as the executable
        cfgPath = std::filesystem::current_path() / "agent.toml";
    }

    spdlog::info("AnyWhereDoor agent starting...");
    spdlog::info("Loading config from: {}", cfgPath.string());

    awd::AgentConfig config;
    try {
        config = awd::AgentConfigLoader::loadFromFile(cfgPath);
    } catch (const std::exception& e) {
        spdlog::critical("Failed to load config: {}", e.what());
        return 1;
    }

    // Persist auto-generated agentId back to disk
    try {
        awd::AgentConfigLoader::saveToFile(config, cfgPath);
    } catch (const std::exception& e) {
        spdlog::warn("Could not save config (agentId may not persist): {}", e.what());
    }

    spdlog::info("Agent ID : {}", config.agentId);
    spdlog::info("Server   : {}:{}", config.serverConfig.host, config.serverConfig.port);
    spdlog::info("Watches  : {} path(s) configured", config.watchEntries.size());
    for (auto& w : config.watchEntries)
        spdlog::info("  -> {}", w.path.string());

    // Initialize and start the EventLoop
    try {
        event::EventLoop eventLoop;
        // g_eventLoop = &eventLoop;

        // Register signal handlers for graceful shutdown
        // std::signal(SIGINT, signalHandler);   // Ctrl+C
        // std::signal(SIGTERM, signalHandler);  // Termination signal

        spdlog::info("Starting EventLoop...");

        // currently the jobs are performed on same threads
        eventLoop.schedule({
            "initialization_task",
            []() {
                spdlog::info("Performing initial setup tasks...");
                // Placeholder for any startup tasks (e.g., initial server connection, health checks)
                std::this_thread::sleep_for(std::chrono::seconds(5)); // Simulate work
                spdlog::info("Initial setup complete.");
            }
        });

        std::thread producer([&]() {
            std::this_thread::sleep_for(std::chrono::seconds(2));

            eventLoop.schedule({
                "task1",
                []() {
                    spdlog::info("Task executed!");
                }
            });
        });
        eventLoop.loop();

        producer.join();

        // // Subscribe to shutdown event to log when shutdown is initiated
        // eventLoop.subscribe(
        //     event::EventType::SHUTDOWN,
        //     [](const std::shared_ptr<event::Event>& evt) {
        //         spdlog::info("Shutdown event received from source: {}", evt->sourceId);
        //     },
        //     "main_shutdown_handler"
        // );

        spdlog::info("EventLoop started successfully. Agent is now active and monitoring...");
        spdlog::info("Press Ctrl+C to shutdown gracefully.");

        // Main thread waits for shutdown signal
        // while (!g_shutdownRequested.load()) {
        //     std::this_thread::sleep_for(std::chrono::milliseconds(100));
        // }

        spdlog::info("Shutdown requested. Stopping EventLoop...");
        eventLoop.stop();

        // Wait for remaining events to be processed
        eventLoop.wait();

        spdlog::info("EventLoop stopped. Agent shutting down gracefully.");
        return 0;

    } catch (const std::exception& e) {
        spdlog::critical("Unexpected error in main event loop: {}", e.what());
        return 1;
    }
}